#include "xhci.hpp"
#include "driver/usb/keyboard/keyboard.hpp"
#include "memory/memory_manager.hpp"
#include "pci/pci.hpp"
#include "printk.hpp"

USB::XHCI::Controller *g_xhci = nullptr;

namespace USB::XHCI
{
    // xHCI Extended Capability ID for Legacy Support
    const uint8_t kCapIdLegacySupport = 1;

    Controller::Controller(const PCI::Device &dev)
        : pci_dev_(dev), mmio_base_(0), dcs_(1), pcs_(1), cmd_ring_index_(0), event_ring_index_(0)
    {
        for (int i = 0; i < 32; ++i)
        {
            transfer_rings_[i] = nullptr;
            ring_cycle_state_[i] = 1;
            ring_index_[i] = 0;
        }
    }

    uint32_t Controller::Read32(uint32_t offset) const
    {
        return *reinterpret_cast<volatile uint32_t *>(mmio_base_ + offset);
    }

    void Controller::Write32(uint32_t offset, uint32_t value)
    {
        *reinterpret_cast<volatile uint32_t *>(mmio_base_ + offset) = value;
    }

    uint32_t Controller::ReadOpReg(uint32_t offset) const
    {
        return *reinterpret_cast<volatile uint32_t *>(op_regs_base_ + offset);
    }

    void Controller::WriteOpReg(uint32_t offset, uint32_t value)
    {
        *reinterpret_cast<volatile uint32_t *>(op_regs_base_ + offset) = value;
    }

    uint32_t Controller::ReadRtReg(uint32_t offset) const
    {
        return *reinterpret_cast<volatile uint32_t *>(rt_regs_base_ + offset);
    }

    void Controller::WriteRtReg(uint32_t offset, uint32_t value)
    {
        *reinterpret_cast<volatile uint32_t *>(rt_regs_base_ + offset) = value;
    }

    void Controller::RingDoorbell(uint8_t target, uint32_t value)
    {
        // Doorbell Register は 32bit 幅の配列
        // Target 0: Host Controller (Command Ring)
        // Target 1-255: Device Slot
        uintptr_t addr = db_regs_base_ + (4 * target);
        *reinterpret_cast<volatile uint32_t *>(addr) = value;
    }

    void Controller::Initialize()
    {
        kprintf("[xHCI] Initializing...\n");

        uint16_t vendor = PCI::ReadConfReg(pci_dev_, 0x00) & 0xFFFF;
        kprintf("[xHCI] Vendor ID: %x\n", vendor);
        if (vendor == 0xFFFF)
        {
            kprintf("[xHCI] CRITICAL ERROR: Device not found or invalid PCI address!\n");
            while (1)
                __asm__ volatile("hlt");
        }

        mmio_base_ = PCI::ReadBar0(pci_dev_);
        kprintf("[xHCI] MMIO Base: %lx\n", mmio_base_);

        uint32_t cmd_reg = PCI::ReadConfReg(pci_dev_, 0x04);
        kprintf("[xHCI] Old Command Reg: %x\n", cmd_reg); // ログで確認

        cmd_reg |= (1 << 2); // Bus Master
        cmd_reg |= (1 << 1); // Memory Space
        PCI::WriteConfReg(pci_dev_, 0x04, cmd_reg);

        kprintf("[xHCI] New Command Reg: %x\n", PCI::ReadConfReg(pci_dev_, 0x04));

        uint8_t cap_length = Read32(0x00) & 0xFF;
        uint32_t rts_offset = Read32(0x18) & ~0x1F; // Runtime Register Space Offset
        uint32_t db_offset = Read32(0x14) & ~0x3;   // Doorbell Offset

        op_regs_base_ = mmio_base_ + cap_length;
        rt_regs_base_ = mmio_base_ + rts_offset;
        db_regs_base_ = mmio_base_ + db_offset;

        BiosHandoff();
        ResetController();

        kprintf("[xHCI] Controller Reset Complete.\n");

        uint32_t hcsparams1 = Read32(0x04);
        max_slots_ = hcsparams1 & 0xFF;
        kprintf("[xHCI] Max Slots: %d\n", max_slots_);

        uint32_t hcsparams2 = Read32(0x08);
        // Hi(bit 25:21) << 5 | Lo(bit 31:27)
        // Intel xHCI Spec:
        //  Bit 31:27 = Max Scratchpad Buffers (Hi)
        //  Bit 25:21 = Max Scratchpad Buffers (Lo)
        uint32_t max_scratchpads = ((hcsparams2 >> 21) & 0x1F) | (((hcsparams2 >> 27) & 0x1F) << 5);

        kprintf("[xHCI] Max Scratchpads: %d\n", max_scratchpads);

        // サイズは (MaxSlots + 1) * 8 バイト。64バイトアライメント必須。
        dcbaa_ = static_cast<uint64_t *>(MemoryManager::Allocate((max_slots_ + 1) * 8, 64));
        for (int i = 0; i <= max_slots_; ++i)
            dcbaa_[i] = 0;

        if (max_scratchpads > 0)
        {
            uint64_t *scratchpad_array = static_cast<uint64_t *>(MemoryManager::Allocate(max_scratchpads * 8, 64));

            for (uint32_t i = 0; i < max_scratchpads; ++i)
            {
                void *buf = MemoryManager::AllocateFrame();
                scratchpad_array[i] = reinterpret_cast<uint64_t>(buf);
            }
            dcbaa_[0] = reinterpret_cast<uint64_t>(scratchpad_array);
        }

        // DCBAAP (OpReg + 0x30) に設定
        uint64_t dcbaa_phys = reinterpret_cast<uint64_t>(dcbaa_);
        WriteOpReg(0x30, dcbaa_phys & 0xFFFFFFFF);
        WriteOpReg(0x34, (dcbaa_phys >> 32));

        // OpReg + 0x38 : Configure Register
        // Bit 0-7: Max Device Slots Enabled (MaxSlotsEn)
        uint32_t config = ReadOpReg(0x38);
        config &= ~0xFF;
        config |= max_slots_; // サポートする最大スロット数を有効化
        WriteOpReg(0x38, config);

        // とりあえずTRB 32個分確保 (サイズ=32*16=512bytes)。64バイトアライメント。
        command_ring_ = static_cast<TRB *>(MemoryManager::Allocate(sizeof(TRB) * 32, 64));
        {
            uint8_t *p = reinterpret_cast<uint8_t *>(command_ring_);
            for (size_t i = 0; i < sizeof(TRB) * 32; ++i)
                p[i] = 0;
        }

        TRB &link_trb = command_ring_[31];                              // 32個目のTRB
        link_trb.parameter = reinterpret_cast<uint64_t>(command_ring_); // 先頭アドレス
        link_trb.status = 0;
        link_trb.control = (6 << 10) | 2; // Type=6, TC(Toggle Cycle)=1

        // CRCR (OpReg + 0x18) に設定。Bit 0 (RCS) は 1 (Cycle Bit) にしておくのが一般的
        uint64_t crcr_phys = reinterpret_cast<uint64_t>(command_ring_);
        WriteOpReg(0x18, (crcr_phys & 0xFFFFFFFF) | 1); // RCS=1
        WriteOpReg(0x1C, (crcr_phys >> 32));

        // Event Ring (TRB x 32)
        event_ring_ = static_cast<TRB *>(MemoryManager::Allocate(sizeof(TRB) * 32, 64));
        {
            uint8_t *p = reinterpret_cast<uint8_t *>(event_ring_);
            for (size_t i = 0; i < sizeof(TRB) * 32; ++i)
                p[i] = 0;
        }

        // ERST (Event Ring Segment Table) - セグメント1つだけ使う
        erst_ = static_cast<EventRingSegmentTableEntry *>(MemoryManager::Allocate(sizeof(EventRingSegmentTableEntry) * 1, 64));
        erst_[0].ring_segment_base_address = reinterpret_cast<uint64_t>(event_ring_);
        erst_[0].ring_segment_size = 32; // TRB数
        erst_[0].reserved = 0;
        erst_[0].reserved2 = 0;

        // Interrupter Register Set 0 (Runtime Registers の先頭 + 0x20 から開始)
        // Interrupter 0 は offset 0x20
        uint32_t int0_offset = 0x20;

        WriteRtReg(int0_offset + 0x08, 1); // Table Size = 1

        uint64_t erdp_phys = reinterpret_cast<uint64_t>(event_ring_);
        WriteRtReg(int0_offset + 0x18, erdp_phys & 0xFFFFFFFF);
        WriteRtReg(int0_offset + 0x1C, erdp_phys >> 32);

        uint64_t erst_phys = reinterpret_cast<uint64_t>(erst_);
        WriteRtReg(int0_offset + 0x10, erst_phys & 0xFFFFFFFF);
        WriteRtReg(int0_offset + 0x14, erst_phys >> 32);

        uint32_t iman = ReadRtReg(int0_offset + 0x00);
        iman |= 2; // Interrupt Enable
        WriteRtReg(int0_offset + 0x00, iman);

        kprintf("[xHCI] Memory Structures Allocated & Registers Set.\n");

        uint32_t usbcmd = ReadOpReg(0x00);
        usbcmd |= 1;
        WriteOpReg(0x00, usbcmd);

        kprintf("[xHCI] Starting Controller...");
        while (ReadOpReg(0x04) & 1)
        {
            __asm__ volatile("pause");
        }
        kprintf(" Running!\n");

        uint32_t op_regs = cap_length;
        // 各ポートの状態を確認 (Port Status and Control Register)
        // PORTSCは OpRegs + 0x400 + (0x10 * (PortNum - 1))
        for (int i = 1; i <= max_slots_ && i <= 16; ++i)
        {
            // 簡易的にMaxPorts変数を再取得するか、Initialize内で保存したローカル変数を使う
            uint32_t portsc_offset = 0x400 + (0x10 * (i - 1));
            uint32_t portsc = ReadOpReg(portsc_offset);

            if (portsc & 1)
            { // Connected
                kprintf("[xHCI] Device found at Port %d. Resetting...\n", i);
                ResetPort(i);

                uint32_t portsc_after = ReadOpReg(portsc_offset);
                int speed = (portsc_after >> 10) & 0x0F;
                kprintf("[xHCI] Port Speed ID: %d\n", speed);

                uint8_t slot_id = EnableSlot();
                if (slot_id > 0)
                {
                    if (AddressDevice(slot_id, i, speed))
                    {
                        g_usb_keyboard = new USB::Keyboard(this, slot_id);
                        if (g_usb_keyboard->Initialize())
                        {
                            kprintf("[xHCI] Keyboard initialized successfully.\n");
                        }
                    }
                }
            }
        }
    }

    bool Controller::ConfigureEndpoint(uint8_t slot_id, uint8_t ep_addr,
                                       uint16_t max_packet_size, uint8_t interval)
    {
        uint8_t dci = AddressToDCI(ep_addr);
        kprintf("[xHCI] Configuring Endpoint %02x (DCI=%d)...\n", ep_addr, dci);

        if (transfer_rings_[dci] == nullptr)
        {
            transfer_rings_[dci] = static_cast<TRB *>(MemoryManager::Allocate(sizeof(TRB) * 32, 64));
            uint8_t *p = reinterpret_cast<uint8_t *>(transfer_rings_[dci]);
            for (size_t i = 0; i < sizeof(TRB) * 32; ++i)
                p[i] = 0;

            ring_cycle_state_[dci] = 1;
            ring_index_[dci] = 0;
        }

        InputContext *input_ctx = static_cast<InputContext *>(MemoryManager::Allocate(sizeof(InputContext), 64));
        {
            uint8_t *p = reinterpret_cast<uint8_t *>(input_ctx);
            for (size_t i = 0; i < sizeof(InputContext); ++i)
                p[i] = 0;
        }

        // Input Control Context
        // Add Context Flags (Bit 0=SlotCtx, Bit DCI=対象EP)
        input_ctx->input_control_context.add_context_flags = (1 << 0) | (1 << dci);

        // Slot Context
        input_ctx->slot_context.context_entries = 31;

        // Endpoint Context
        EndpointContext &ep_ctx = input_ctx->ep_contexts[dci - 1];

        ep_ctx.ep_type = (ep_addr & 0x80) ? 7 : 3; // 7=Interrupt IN, 3=Interrupt OUT
        ep_ctx.max_packet_size = max_packet_size;
        ep_ctx.interval = interval; // Descriptorから取った値を設定
        ep_ctx.average_trb_length = 1;

        ep_ctx.error_count = 3;

        ep_ctx.max_burst_size = 0;

        uint64_t ring_base = reinterpret_cast<uint64_t>(transfer_rings_[dci]);
        ep_ctx.dequeue_pointer = ring_base | 1; // DCS=1

        uint64_t command_trb_ptr = reinterpret_cast<uint64_t>(&command_ring_[cmd_ring_index_]);

        TRB &trb = command_ring_[cmd_ring_index_];
        trb.parameter = reinterpret_cast<uint64_t>(input_ctx);
        trb.status = 0;
        // Type=12 (Configure Endpoint)
        trb.control = (pcs_ & 1) | (12 << 10) | (slot_id << 24);

        __asm__ volatile("wbinvd");
        cmd_ring_index_++;
        RingDoorbell(0, 0);

        int timeout = 1000000;
        while (timeout > 0)
        {
            __asm__ volatile("wbinvd");
            volatile TRB &event = event_ring_[event_ring_index_];
            uint32_t control = event.control;

            if ((control & 1) == dcs_)
            {
                event_ring_index_++;
                uint32_t trb_type = (control >> 10) & 0x3F;
                uint64_t param = event.parameter;

                if ((trb_type == 32 || trb_type == 33) && param == command_trb_ptr)
                {
                    uint32_t code = (event.status >> 24) & 0xFF;
                    MemoryManager::Free(input_ctx, sizeof(InputContext));

                    if (code == 1)
                    {
                        kprintf("[xHCI] Endpoint %x Configured!\n", ep_addr);
                        return true;
                    }
                    else
                    {
                        kprintf("[xHCI] Configure Endpoint Failed. Code=%d\n", code);
                        return false;
                    }
                }
            }
            else
            {
                timeout--;
                __asm__ volatile("pause");
            }
        }

        MemoryManager::Free(input_ctx, sizeof(InputContext));
        kprintf("[xHCI] Configure Endpoint Timeout.\n");
        return false;
    }

    bool Controller::ControlIn(uint8_t slot_id, uint8_t req_type, uint8_t request,
                               uint16_t value, uint16_t index,
                               uint16_t length, void *buffer)
    {
        TRB &setup_trb = transfer_rings_[1][ring_index_[1]++];

        // Setup Parameter: (Length << 48) | (Index << 32) | (Value << 16) | (Request << 8) | ReqType
        setup_trb.parameter = (static_cast<uint64_t>(length) << 48) |
                              (static_cast<uint64_t>(index) << 32) |
                              (static_cast<uint64_t>(value) << 16) |
                              (static_cast<uint64_t>(request) << 8) |
                              req_type;

        setup_trb.status = 8;                                                              // Setup Packet Size
        setup_trb.control = (ring_cycle_state_[1] & 1) | (2 << 10) | (1 << 6) | (3 << 16); // IDT, Setup, In Data

        if (length > 0)
        {
            TRB &data_trb = transfer_rings_[1][ring_index_[1]++];
            data_trb.parameter = reinterpret_cast<uint64_t>(buffer);
            data_trb.status = length;
            data_trb.control = (ring_cycle_state_[1] & 1) | (3 << 10) | (1 << 16) | (1 << 5); // Data, In, IOC
        }

        TRB &status_trb = transfer_rings_[1][ring_index_[1]++];
        status_trb.parameter = 0;
        status_trb.status = 0;
        // SetupがINならStatusはOUT(Dir=0)
        status_trb.control = (ring_cycle_state_[1] & 1) | (4 << 10) | (1 << 1);

        RingDoorbell(slot_id, 1);

        int timeout = 1000000;
        while (timeout > 0)
        {
            __asm__ volatile("wbinvd");
            volatile TRB &event = event_ring_[event_ring_index_];
            uint32_t control = event.control;

            if ((control & 1) == dcs_)
            {
                event_ring_index_++;
                uint32_t trb_type = (control >> 10) & 0x3F;
                // Transfer Event (Type 32)
                if (trb_type == 32)
                {
                    uint32_t comp_code = (event.status >> 24) & 0xFF;
                    if (comp_code == 1 || comp_code == 13)
                        return true;
                    else
                    {
                        kprintf("[xHCI] ControlIn Failed. Code=%d\n", comp_code);
                        return false;
                    }
                }
            }
            else
            {
                timeout--;
                __asm__ volatile("pause");
            }
        }
        kprintf("[xHCI] ControlIn Timeout.\n");
        return false;
    }

    int Controller::PollEndpoint(uint8_t slot_id, uint8_t ep_addr)
    {
        __asm__ volatile("wbinvd");
        volatile TRB &event = event_ring_[event_ring_index_];
        uint32_t control = event.control;

        if ((control & 1) == dcs_)
        {
            int ret_code = 0;
            uint32_t trb_type = (control >> 10) & 0x3F;

            if (trb_type == 32)
            {
                uint8_t event_slot = (control >> 24) & 0xFF;
                uint8_t event_dci = (control >> 16) & 0x1F;

                uint8_t target_dci = AddressToDCI(ep_addr);

                if (event_slot == slot_id && event_dci == target_dci)
                {
                    uint32_t comp_code = (event.status >> 24) & 0xFF;
                    ret_code = comp_code;
                }
            }
            event_ring_index_++;

            if (event_ring_index_ == 32)
            {
                event_ring_index_ = 0;
                dcs_ ^= 1;
            }

            uint64_t erdp = reinterpret_cast<uint64_t>(&event_ring_[event_ring_index_]);

            WriteRtReg(0x20 + 0x18, (erdp & 0xFFFFFFFF) | (1 << 3));
            WriteRtReg(0x20 + 0x1C, (erdp >> 32));

            return ret_code;
        }

        return -1;
    }

    bool Controller::SendNormalTRB(uint8_t slot_id, uint8_t ep_addr, void *data_buf, uint32_t len)
    {
        uint8_t dci = AddressToDCI(ep_addr);
        TRB *ring = transfer_rings_[dci];
        if (!ring)
            return false;

        uint32_t idx = ring_index_[dci];
        uint8_t pcs = ring_cycle_state_[dci];

        TRB &trb = ring[idx];
        trb.parameter = reinterpret_cast<uint64_t>(data_buf);
        trb.status = len; // Transfer Length

        // Type=1 (Normal), IOC=1 (完了時にイベント発生), ISP=1 (Short Packet時もイベント)
        trb.control = (pcs & 1) | (1 << 10) | (1 << 5) | (1 << 2);

        ring_index_[dci]++;

        if (ring_index_[dci] == 31)
        {
            TRB &link = ring[31];
            link.parameter = reinterpret_cast<uint64_t>(ring);
            link.status = 0;
            link.control = (pcs & 1) | (6 << 10) | (1 << 1);
            ring_cycle_state_[dci] ^= 1;
            ring_index_[dci] = 0;
        }

        __asm__ volatile("wbinvd");
        RingDoorbell(slot_id, dci);

        return true;
    }

    void Controller::BiosHandoff()
    {
        // Capability Register (MMIO先頭) から HCCPARAMS1 (Offset 0x10) を読む
        uint32_t hccparams1 = Read32(0x10);

        // Extended Capabilities Pointer (xECP) は bit 31-16
        // この値は "dwords" (4バイト単位) のオフセット
        uint32_t xecp_offset = (hccparams1 >> 16) & 0xFFFF;

        if (xecp_offset == 0)
        {
            kprintf("[xHCI] No Extended Capabilities found.\n");
            return;
        }

        // 4バイト単位なのでバイトオフセットに変換
        uint32_t current_offset = xecp_offset << 2;

        while (true)
        {
            uint32_t reg = Read32(current_offset);
            uint8_t cap_id = reg & 0xFF;

            if (cap_id == kCapIdLegacySupport)
            {
                kprintf("[xHCI] Found USB Legacy Support at offset %x\n", current_offset);

                // USBLEGSUP レジスタ
                // Bit 16: HC BIOS Owned Semaphore
                // Bit 24: HC OS Owned Semaphore

                if (reg & (1 << 16)) // BIOSが持っている場合
                {
                    kprintf("[xHCI] Requesting OS Ownership...\n");
                    // OS Owned (bit 24) を立てる
                    Write32(current_offset, reg | (1 << 24));

                    // BIOS Owned (bit 16) が落ちるのを待つ
                    kprintf("[xHCI] Waiting for BIOS handoff...");
                    int timeout = 1000000;
                    while (true)
                    {
                        uint32_t val = Read32(current_offset);
                        // BIOS Ownedが0になり、OS Ownedが1なら成功
                        if (!(val & (1 << 16)) && (val & (1 << 24)))
                            break;

                        timeout--;
                        if (timeout == 0)
                        {
                            kprintf(" Timeout!\n");
                            break;
                        }
                        __asm__ volatile("pause");
                    }
                    kprintf(" Done.\n");
                }
                break; // Handoff完了したらループを抜ける
            }

            // 次のCapabilityへ (Next Capability Pointer: bit 15-8)
            uint8_t next = (reg >> 8) & 0xFF;
            if (next == 0)
                break;

            current_offset += (next << 2);
        }
    }

    bool Controller::AddressDevice(uint8_t slot_id, int port_id, int speed)
    {
        DeviceContext *out_ctx = static_cast<DeviceContext *>(MemoryManager::Allocate(sizeof(DeviceContext), 64));

        {
            uint8_t *p = reinterpret_cast<uint8_t *>(out_ctx);
            for (size_t i = 0; i < sizeof(DeviceContext); ++i)
                p[i] = 0;
        }

        dcbaa_[slot_id] = reinterpret_cast<uint64_t>(out_ctx);

        InputContext *input_ctx = static_cast<InputContext *>(MemoryManager::Allocate(sizeof(InputContext), 64));

        {
            uint8_t *p = reinterpret_cast<uint8_t *>(input_ctx);
            for (size_t i = 0; i < sizeof(InputContext); ++i)
                p[i] = 0;
        }

        // --- Input Control Context の設定 ---
        // A0 (Slot Context) と A1 (Endpoint 0) を有効にする (Bit 0 と Bit 1)
        input_ctx->input_control_context.add_context_flags = (1 << 0) | (1 << 1);

        // --- Slot Context の設定 ---
        input_ctx->slot_context.root_hub_port_num = port_id; // 接続されているポート番号
        input_ctx->slot_context.route_string = 0;
        input_ctx->slot_context.context_entries = 1; // EP0まで有効
        input_ctx->slot_context.speed = speed;       // ポートから読み取った速度

        // --- Endpoint Context 0 (Control Pipe) の設定 ---
        input_ctx->ep_contexts[0].ep_type = 4; // Control Endpoint (Bidirectional)

        transfer_rings_[1] = static_cast<TRB *>(MemoryManager::Allocate(sizeof(TRB) * 32, 64));
        {
            uint8_t *p = reinterpret_cast<uint8_t *>(transfer_rings_[1]);
            for (size_t i = 0; i < sizeof(TRB) * 32; ++i)
                p[i] = 0;
        }
        uint64_t tr_phys = reinterpret_cast<uint64_t>(transfer_rings_[1]);
        if (speed == 4)
            input_ctx->ep_contexts[0].max_packet_size = 512;
        else if (speed == 3)
            input_ctx->ep_contexts[0].max_packet_size = 64;
        else
            input_ctx->ep_contexts[0].max_packet_size = 8; // Low/Fullはとりあえず8 (後で読むDescriptorで確定)

        input_ctx->ep_contexts[0].max_burst_size = 0;
        input_ctx->ep_contexts[0].dequeue_pointer = tr_phys;
        input_ctx->ep_contexts[0].dequeue_pointer |= 1;
        input_ctx->ep_contexts[0].interval = 0;
        input_ctx->ep_contexts[0].average_trb_length = 8;
        input_ctx->ep_contexts[0].error_count = 3;

        uint64_t command_trb_ptr = reinterpret_cast<uint64_t>(&command_ring_[cmd_ring_index_]);

        TRB &trb = command_ring_[cmd_ring_index_];
        trb.parameter = reinterpret_cast<uint64_t>(input_ctx); // Input Contextのポインタ
        trb.status = 0;
        // Type=11 (Address Device), Slot IDを設定
        trb.control = (pcs_ & 1) | (TRB_ADDRESS_DEVICE << 10) | (slot_id << 24);

        __asm__ volatile("wbinvd");
        cmd_ring_index_++;
        RingDoorbell(0, 0);

        kprintf("[xHCI] Sent Address Device Command (Slot %d, Speed %d). Waiting...\n", slot_id, speed);

        int timeout = 1000000;
        while (timeout > 0)
        {
            __asm__ volatile("wbinvd");
            volatile TRB &event = event_ring_[event_ring_index_];
            uint32_t control = event.control;

            if ((control & 1) == dcs_)
            {
                event_ring_index_++;
                uint32_t trb_type = (control >> 10) & 0x3F;
                uint64_t param = event.parameter;

                if ((trb_type == TRB_COMMAND_COMPLETION || trb_type == 33) && param == command_trb_ptr)
                {
                    uint8_t comp_code = (event.status >> 24) & 0xFF;
                    if (comp_code == 1) // Success
                    {
                        kprintf("[xHCI] Address Device Successful! Slot %d is active.\n", slot_id);
                        return true;
                    }
                    else
                    {
                        kprintf("[xHCI] Address Device Failed. Code: %d\n", comp_code);
                        return false;
                    }
                }
            }
            else
            {
                timeout--;
                __asm__ volatile("pause");
            }
        }

        kprintf("[xHCI] TIMEOUT: Address Device Command ignored.\n");
        return false;
    }

    uint8_t Controller::EnableSlot()
    {
        uint64_t command_trb_ptr = reinterpret_cast<uint64_t>(&command_ring_[cmd_ring_index_]);
        TRB &trb = command_ring_[cmd_ring_index_];
        trb.parameter = 0;
        trb.status = 0;

        // Control Field:
        // Bit 0: Cycle Bit (PCS)
        // Bit 10-15: TRB Type (Enable Slot = 9)
        // Bit 16-23: Slot Type (0)
        trb.control = (pcs_ & 1) | (TRB_ENABLE_SLOT << 10);

        cmd_ring_index_++;

        __asm__ volatile("wbinvd");

        RingDoorbell(0, 0);

        kprintf("[xHCI] Sent Enable Slot Command. Waiting for completion...\n");
        int timeout = 10000000;
        while (timeout > 0)
        {
            uint32_t usbsts = ReadOpReg(0x04);
            if (usbsts & (1 << 2))
            {
                kprintf("\n[xHCI] FATAL ERROR: Host System Error detected! (USBSTS=%x)\n", usbsts);
                while (1)
                    __asm__ volatile("hlt");
            }

            volatile TRB &event = event_ring_[event_ring_index_];
            uint32_t control = event.control;

            if ((control & 1) != dcs_)
            {
                timeout--;
                __asm__ volatile("pause");
                continue;
            }
            event_ring_index_++;

            // TRB Type チェック (Bit 10-15)
            uint32_t trb_type = (control >> 10) & 0x3F;
            uint64_t param = event.parameter;
            if ((trb_type == TRB_COMMAND_COMPLETION || trb_type == 33) && param == command_trb_ptr)
            {
                uint8_t comp_code = (event.status >> 24) & 0xFF;
                if (comp_code == 1) // Success
                {
                    uint8_t slot_id = (control >> 24) & 0xFF;
                    kprintf("[xHCI] Slot ID %d assigned successfully!\n", slot_id);
                    return slot_id;
                }
                else
                {
                    kprintf("[xHCI] Enable Slot Failed. Code: %d\n", comp_code);
                    return 0;
                }
            }
            timeout--;
            __asm__ volatile("pause");
        }
        kprintf("[xHCI] TIMEOUT: Enable Slot Command ignored.\n");

        uint32_t crcr_low = ReadOpReg(0x18);
        uint32_t crcr_high = ReadOpReg(0x1C);
        kprintf("Debug CRCR: %x %x\n", crcr_high, crcr_low);

        uint32_t usbsts = ReadOpReg(0x04);
        kprintf("Debug USBSTS: %x\n", usbsts);

        return 0;
    }

    void Controller::ResetController()
    {
        uint8_t cap_length = Read32(0x00) & 0xFF;
        uint32_t op_regs_base = cap_length; // バイト単位のオフセット

        // USBCMD (USB Command) は OpRegs のオフセット 0x00
        // USBSTS (USB Status)  は OpRegs のオフセット 0x04

        uint32_t usbcmd_offset = op_regs_base + 0x00;
        uint32_t usbsts_offset = op_regs_base + 0x04;

        uint32_t usbcmd = Read32(usbcmd_offset);
        usbcmd &= ~(1U); // Bit 0 (Run/Stop) をクリア
        Write32(usbcmd_offset, usbcmd);

        while (!(Read32(usbsts_offset) & 1))
        {
            __asm__ volatile("pause");
        }

        usbcmd = Read32(usbcmd_offset);
        usbcmd |= (1U << 1);
        Write32(usbcmd_offset, usbcmd);

        while (Read32(usbcmd_offset) & (1U << 1))
        {
            __asm__ volatile("pause");
        }

        while (Read32(usbsts_offset) & (1U << 11))
        {
            __asm__ volatile("pause");
        }
    }

    void Controller::ResetPort(int port_id)
    {
        uint32_t portsc_offset = 0x400 + (0x10 * (port_id - 1));
        uint32_t portsc = ReadOpReg(portsc_offset);

        if (!(portsc & 1))
            return; // Current Connect Status (CCS)

        kprintf("[xHCI] Resetting Port %d...\n", port_id);

        portsc = (portsc & 0x0E00C3E0); // Preserve RO/RW bits mask (簡易的)
        portsc |= (1 << 4);             // Set Port Reset
        WriteOpReg(portsc_offset, portsc);

        while (true)
        {
            uint32_t val = ReadOpReg(portsc_offset);
            if (val & (1 << 21)) // Port Reset Change
            {
                WriteOpReg(portsc_offset, (val & 0x0E00C3E0) | (1 << 21));
                break;
            }
            __asm__ volatile("pause");
        }

        kprintf("[xHCI] Port %d Reset Complete. Checking status...\n", port_id);

        uint32_t after = ReadOpReg(portsc_offset);
        if (after & (1 << 1))
        {
            kprintf("[xHCI] Port %d is Enabled!\n", port_id);
        }
        else
        {
            kprintf("[xHCI] Port %d Reset Failed (Not Enabled).\n", port_id);
        }
    }
}