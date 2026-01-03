#include "xhci.hpp"
#include "arch/inasm.hpp"
#include "driver/usb/keyboard/keyboard.hpp"
#include "driver/usb/mass_storage/mass_storage.hpp"
#include "memory/memory_manager.hpp"
#include "paging.hpp"
#include "pci/pci.hpp"
#include "printk.hpp"

USB::XHCI::Controller *g_xhci = nullptr;

namespace USB::XHCI
{
// xHCI Extended Capability ID for Legacy Support
const uint8_t kCapIdLegacySupport = 1;

Controller::Controller(const PCI::Device &dev)
    : pci_dev_(dev), mmio_base_(0), dcs_(1), pcs_(1), cmd_ring_index_(0),
      event_ring_index_(0)
{
    for (int slot = 0; slot < 256; ++slot)
    {
        for (int i = 0; i < 32; ++i)
        {
            transfer_rings_[slot][i] = nullptr;
            ring_cycle_state_[slot][i] = 1;
            ring_index_[slot][i] = 0;
        }
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
        kprintf("[xHCI] CRITICAL ERROR: Device not found or invalid PCI "
                "address!\n");
        while (1)
            Hlt();
    }

    mmio_base_ = PCI::ReadBar0(pci_dev_);
    kprintf("[xHCI] MMIO Base: %lx\n", mmio_base_);

#if defined(__aarch64__)
    // xHCI MMIO領域をDevice Memoryとしてマッピング (64KB)
    PageManager::MapPage(mmio_base_, mmio_base_, 16,
                         PageManager::kPresent | PageManager::kWritable |
                             PageManager::kDevice);
    kprintf("[xHCI] Mapped MMIO at %lx (64KB)\n", mmio_base_);
#endif

    uint32_t cmd_reg = PCI::ReadConfReg(pci_dev_, 0x04);
    kprintf("[xHCI] Old Command Reg: %x\n", cmd_reg); // ログで確認

    cmd_reg |= (1 << 2); // Bus Master
    cmd_reg |= (1 << 1); // Memory Space
    PCI::WriteConfReg(pci_dev_, 0x04, cmd_reg);

    kprintf("[xHCI] New Command Reg: %x\n", PCI::ReadConfReg(pci_dev_, 0x04));

    DSB(); // メモリバリア: MMIO読み込み前に書き込み完了を保証

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
    max_ports_ = (hcsparams1 >> 24) & 0xFF;
    kprintf("[xHCI] Max Slots: %d, Max Ports: %d\n", max_slots_, max_ports_);

    uint32_t hcsparams2 = Read32(0x08);
    // Hi(bit 25:21) << 5 | Lo(bit 31:27)
    // Intel xHCI Spec:
    //  Bit 31:27 = Max Scratchpad Buffers (Hi)
    //  Bit 25:21 = Max Scratchpad Buffers (Lo)
    uint32_t max_scratchpads =
        ((hcsparams2 >> 21) & 0x1F) | (((hcsparams2 >> 27) & 0x1F) << 5);

    kprintf("[xHCI] Max Scratchpads: %d\n", max_scratchpads);

    // サイズは (MaxSlots + 1) * 8 バイト。64バイトアライメント必須。
    dcbaa_ = static_cast<uint64_t *>(
        MemoryManager::Allocate((max_slots_ + 1) * 8, 64));
    PageManager::SetDeviceMemory(dcbaa_, (max_slots_ + 1) * 8);

    for (int i = 0; i <= max_slots_; ++i)
        dcbaa_[i] = 0;
    FlushCache(dcbaa_,
               (max_slots_ + 1) * 8); // AArch64: DMA前にキャッシュフラッシュ

    if (max_scratchpads > 0)
    {
        uint64_t *scratchpad_array = static_cast<uint64_t *>(
            MemoryManager::Allocate(max_scratchpads * 8, 64));
        PageManager::SetDeviceMemory(scratchpad_array, max_scratchpads * 8);

        for (uint32_t i = 0; i < max_scratchpads; ++i)
        {
            void *buf = MemoryManager::AllocateFrame();
            scratchpad_array[i] = reinterpret_cast<uint64_t>(buf);
        }
        FlushCache(scratchpad_array,
                   max_scratchpads *
                       8); // AArch64: Scratchpadポインタをフラッシュ
        dcbaa_[0] = reinterpret_cast<uint64_t>(scratchpad_array);
        FlushCache(&dcbaa_[0],
                   sizeof(uint64_t)); // AArch64: DCBAA[0]を再フラッシュ
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
    command_ring_ =
        static_cast<TRB *>(MemoryManager::Allocate(sizeof(TRB) * 32, 64));
    PageManager::SetDeviceMemory(command_ring_, sizeof(TRB) * 32);
    {
        uint8_t *p = reinterpret_cast<uint8_t *>(command_ring_);
        for (size_t i = 0; i < sizeof(TRB) * 32; ++i)
            p[i] = 0;
    }

    TRB &link_trb = command_ring_[31]; // 32個目のTRB
    link_trb.parameter =
        reinterpret_cast<uint64_t>(command_ring_); // 先頭アドレス
    link_trb.status = 0;
    link_trb.control = (6 << 10) | 2; // Type=6, TC(Toggle Cycle)=1
    FlushCache(command_ring_,
               sizeof(TRB) * 32); // AArch64: Command Ringをフラッシュ

    // CRCR (OpReg + 0x18) に設定。Bit 0 (RCS) は 1 (Cycle Bit)
    // にしておくのが一般的
    uint64_t crcr_phys = reinterpret_cast<uint64_t>(command_ring_);
    WriteOpReg(0x18, (crcr_phys & 0xFFFFFFFF) | 1); // RCS=1
    WriteOpReg(0x1C, (crcr_phys >> 32));

    // Event Ring (TRB x 32)
    // 注意: Event RingはDevice Memory属性を設定しない。
    // xHCIコントローラがDMAで書き込むデータを正しく読み取るため、
    // Normal Memoryとして確保し、読み取り前にキャッシュインバリデートを行う。
    event_ring_ =
        static_cast<TRB *>(MemoryManager::Allocate(sizeof(TRB) * 32, 64));
    // PageManager::SetDeviceMemory(event_ring_, sizeof(TRB) * 32); // 削除:
    // Normal Memoryのまま
    {
        uint8_t *p = reinterpret_cast<uint8_t *>(event_ring_);
        for (size_t i = 0; i < sizeof(TRB) * 32; ++i)
            p[i] = 0;
    }
    FlushCache(event_ring_,
               sizeof(TRB) * 32); // AArch64: Event Ringをフラッシュ

    // ERST (Event Ring Segment Table) - セグメント1つだけ使う
    erst_ = static_cast<EventRingSegmentTableEntry *>(
        MemoryManager::Allocate(sizeof(EventRingSegmentTableEntry) * 1, 64));
    PageManager::SetDeviceMemory(erst_, sizeof(EventRingSegmentTableEntry) * 1);
    erst_[0].ring_segment_base_address =
        reinterpret_cast<uint64_t>(event_ring_);
    erst_[0].ring_segment_size = 32; // TRB数
    erst_[0].reserved = 0;
    erst_[0].reserved2 = 0;
    FlushCache(erst_, sizeof(EventRingSegmentTableEntry) *
                          1); // AArch64: ERSTをフラッシュ（重要！）
    DSB(); // メモリバリア: xHCIレジスタ書き込み前にDRAMへの書き込み完了を保証

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

#if defined(__x86_64__)
    iman |= 2; // Interrupt Enable
#else
    iman &= ~2; // Interrupt Disable (Force Polling to avoid race condition)
#endif
    WriteRtReg(int0_offset + 0x00, iman);

    kprintf("[xHCI] Memory Structures Allocated & Registers Set.\n");

    // MSI/MSI-X割り込みを設定 (ベクタ 0x50)
#if defined(__aarch64__)
    kprintf("[PCI MSI] MSI not fully supported on AArch64 yet. Skipping.\n");
#else
    kprintf("[xHCI] Setting up MSI/MSI-X interrupts...\n");
    if (PCI::SetupMSI(pci_dev_, 0x50))
    {
        kprintf("[xHCI] MSI/MSI-X setup successful.\n");
    }
    else
    {
        kprintf("[xHCI] Warning: MSI/MSI-X setup failed, interrupts may not "
                "work.\n");
    }
#endif

    uint32_t usbcmd = ReadOpReg(0x00);
    usbcmd |= 1;
    WriteOpReg(0x00, usbcmd);

    kprintf("[xHCI] Starting Controller...");
    while (ReadOpReg(0x04) & 1)
    {
        PAUSE();
    }
    kprintf(" Running!\n");
    kprintf("[xHCI] DEBUG: Controller Started. Checking Ports...\n");

    uint32_t op_regs = cap_length;
    // 各ポートの状態を確認 (Port Status and Control Register)
    // PORTSCは OpRegs + 0x400 + (0x10 * (PortNum - 1))
    for (int i = 1; i <= max_ports_; ++i)
    {
        // 簡易的にMaxPorts変数を再取得するか、Initialize内で保存したローカル変数を使う
        uint32_t portsc_offset = 0x400 + (0x10 * (i - 1));
        uint32_t portsc = ReadOpReg(portsc_offset);

        if (portsc & 1)
        { // Connected
            kprintf("[xHCI] DEBUG: Device found at Port %d. Status: %x. "
                    "Resetting...\n",
                    i, portsc);
            ResetPort(i);
            kprintf("[xHCI] DEBUG: Port %d Reset returned.\n", i);

            uint32_t portsc_after = ReadOpReg(portsc_offset);
            int speed = (portsc_after >> 10) & 0x0F;
            kprintf("[xHCI] Port Speed ID: %d\n", speed);

            uint8_t slot_id = EnableSlot();
            if (slot_id > 0)
            {
                if (AddressDevice(slot_id, i, speed))
                {
                    DeviceDescriptor dev_desc;
                    if (ControlIn(slot_id, 0x80, 6, 0x0100, 0, 18, &dev_desc))
                    {
                        g_usb_keyboard = new USB::Keyboard(this, slot_id);
                        if (g_usb_keyboard->Initialize())
                        {
                            kprintf("[xHCI - kbd] Keyboard initialized.\n");
                            // ERDP確認
                            uint32_t el = ReadRtReg(0x20 + 0x18);
                            uint32_t eh = ReadRtReg(0x20 + 0x1C);
                            kprintf("[xHCI DBG] After kbd init: ERDP=0x%x%08x, "
                                    "idx=%d\n",
                                    eh, el, event_ring_index_);
                        }
                        else
                        {
                            delete g_usb_keyboard;
                            g_usb_keyboard = nullptr;

                            // ERDP確認（Mass Storage初期化前）
                            uint32_t el = ReadRtReg(0x20 + 0x18);
                            uint32_t eh = ReadRtReg(0x20 + 0x1C);
                            kprintf("[xHCI DBG] Before MSC init: "
                                    "ERDP=0x%x%08x, idx=%d\n",
                                    eh, el, event_ring_index_);

                            g_mass_storage =
                                new USB::MassStorage(this, slot_id);
                            if (g_mass_storage->Initialize())
                            {
                                kprintf(
                                    "[xHCI - ms] Mass Storage initialized!\n");

                                uint8_t *sec0 =
                                    (uint8_t *)MemoryManager::Allocate(512, 64);
                                if (g_mass_storage->ReadSectors(0, 1, sec0))
                                {
                                    kprintf("Sector 0 Dump: %x %x ...\n",
                                            sec0[0], sec0[1]);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

bool Controller::ConfigureEndpoint(uint8_t slot_id, uint8_t ep_addr,
                                   uint16_t max_packet_size, uint8_t interval,
                                   uint8_t type)
{
    uint8_t dci = AddressToDCI(ep_addr);
    kprintf("[xHCI] Configuring Endpoint %x (DCI=%d)...\n", ep_addr, dci);

    if (transfer_rings_[slot_id][dci] == nullptr)
    {
        transfer_rings_[slot_id][dci] =
            static_cast<TRB *>(MemoryManager::Allocate(sizeof(TRB) * 32, 64));
        PageManager::SetDeviceMemory(transfer_rings_[slot_id][dci],
                                     sizeof(TRB) * 32);
        uint8_t *p = reinterpret_cast<uint8_t *>(transfer_rings_[slot_id][dci]);
        for (size_t i = 0; i < sizeof(TRB) * 32; ++i)
            p[i] = 0;
        FlushCache(transfer_rings_[slot_id][dci], sizeof(TRB) * 32);

        ring_cycle_state_[slot_id][dci] = 1;
        ring_index_[slot_id][dci] = 0;
    }

    InputContext *input_ctx = static_cast<InputContext *>(
        MemoryManager::Allocate(sizeof(InputContext), 64));
    PageManager::SetDeviceMemory(input_ctx, sizeof(InputContext));
    {
        uint8_t *p = reinterpret_cast<uint8_t *>(input_ctx);
        for (size_t i = 0; i < sizeof(InputContext); ++i)
            p[i] = 0;
    }
    FlushCache(input_ctx, sizeof(InputContext));

    // Input Control Context
    // Add Context Flags (Bit 0=SlotCtx, Bit DCI=対象EP)
    input_ctx->input_control_context.add_context_flags = (1 << 0) | (1 << dci);

    // Slot Context
    input_ctx->slot_context.context_entries = 31;

    // Endpoint Context
    EndpointContext &ep_ctx = input_ctx->ep_contexts[dci - 1];

    if (type == 2) // Bulk
    {
        ep_ctx.ep_type = (ep_addr & 0x80) ? 6 : 2; // 6=Bulk IN, 2=Bulk OUT
    }
    else // Interrupt (Assuming type 3)
    {
        ep_ctx.ep_type =
            (ep_addr & 0x80) ? 7 : 3; // 7=Interrupt IN, 3=Interrupt OUT
    }
    ep_ctx.max_packet_size = max_packet_size;
    ep_ctx.interval = interval; // Descriptorから取った値を設定

    // Average TRB Length: Bulkの場合は平均パケットサイズ程度に設定
    // 小さすぎると帯域幅計算でおかしくなる可能性あり
    ep_ctx.average_trb_length = (type == 2) ? 512 : 8;

    ep_ctx.error_count = 3;

    ep_ctx.max_burst_size = 0;

    uint64_t ring_base =
        reinterpret_cast<uint64_t>(transfer_rings_[slot_id][dci]);
    ep_ctx.dequeue_pointer = ring_base | 1; // DCS=1

    // InputContext設定完了後にフラッシュ
    FlushCache(input_ctx, sizeof(InputContext));

    uint64_t command_trb_ptr =
        reinterpret_cast<uint64_t>(&command_ring_[cmd_ring_index_]);

    TRB &trb = command_ring_[cmd_ring_index_];
    trb.parameter = reinterpret_cast<uint64_t>(input_ctx);
    trb.status = 0;
    // Type=12 (Configure Endpoint)
    trb.control = (pcs_ & 1) | (12 << 10) | (slot_id << 24);

    // コマンドTRBのフラッシュ
    FlushCache(&trb, sizeof(TRB));

    DSB();
    cmd_ring_index_++;
    RingDoorbell(0, 0);

    int timeout = 1000000;
    while (timeout > 0)
    {
        DSB();
        InvalidateCache(&event_ring_[event_ring_index_], sizeof(TRB));
        volatile TRB &event = event_ring_[event_ring_index_];
        uint32_t control = event.control;

        if ((control & 1) == dcs_)
        {
            AdvanceEventRing();
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
                    kprintf("[xHCI] Configure Endpoint Failed. Code=%d\n",
                            code);
                    return false;
                }
            }
            else
            {
                kprintf("[xHCI DBG] ConfigureEndpoint: consumed unexpected "
                        "event type=%d\n",
                        trb_type);
            }
        }
        else
        {
            timeout--;
            PAUSE();
        }
    }

    MemoryManager::Free(input_ctx, sizeof(InputContext));
    kprintf("[xHCI] Configure Endpoint Timeout.\n");
    return false;
}

bool Controller::ControlIn(uint8_t slot_id, uint8_t req_type, uint8_t request,
                           uint16_t value, uint16_t index, uint16_t length,
                           void *buffer)
{
    uint32_t start_idx = ring_index_[slot_id][1]; // 開始インデックスを保存

    TRB &setup_trb = transfer_rings_[slot_id][1][ring_index_[slot_id][1]++];

    // Setup Parameter: (Length << 48) | (Index << 32) | (Value << 16) |
    // (Request << 8) | ReqType
    setup_trb.parameter = (static_cast<uint64_t>(length) << 48) |
                          (static_cast<uint64_t>(index) << 32) |
                          (static_cast<uint64_t>(value) << 16) |
                          (static_cast<uint64_t>(request) << 8) | req_type;

    setup_trb.status = 8; // Setup Packet Size
    setup_trb.control = (ring_cycle_state_[slot_id][1] & 1) | (2 << 10) |
                        (1 << 6) | (3 << 16); // IDT, Setup, In Data

    if (length > 0)
    {
        TRB &data_trb = transfer_rings_[slot_id][1][ring_index_[slot_id][1]++];
        data_trb.parameter = reinterpret_cast<uint64_t>(buffer);
        data_trb.status = length;
        data_trb.control = (ring_cycle_state_[slot_id][1] & 1) | (3 << 10) |
                           (1 << 16) | (1 << 5); // Data, In, IOC
    }

    TRB &status_trb = transfer_rings_[slot_id][1][ring_index_[slot_id][1]++];
    status_trb.parameter = 0;
    status_trb.status = 0;
    // SetupがINならStatusはOUT(Dir=0)
    status_trb.control =
        (ring_cycle_state_[slot_id][1] & 1) | (4 << 10) | (1 << 1);

    // AArch64: 全TRBをキャッシュフラッシュしてからDoorbellを鳴らす
    uint32_t trb_count = ring_index_[slot_id][1] - start_idx;
    FlushCache(&transfer_rings_[slot_id][1][start_idx],
               sizeof(TRB) * trb_count);
    DSB();

    RingDoorbell(slot_id, 1);

    int timeout = 1000000;
    while (timeout > 0)
    {
        DSB();
        InvalidateCache(&event_ring_[event_ring_index_], sizeof(TRB));
        volatile TRB &event = event_ring_[event_ring_index_];
        uint32_t control = event.control;

        if ((control & 1) == dcs_)
        {
            AdvanceEventRing();
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
            else
            {
                // Transfer Event以外のイベントを消費した場合はログ出力
                kprintf("[xHCI DBG] ControlIn: consumed non-Transfer Event "
                        "type=%d\n",
                        trb_type);
            }
        }
        else
        {
            timeout--;
            PAUSE();
        }
    }
    kprintf("[xHCI] ControlIn Timeout.\n");
    return false;
}

int Controller::PollEndpoint(uint8_t slot_id, uint8_t ep_addr)
{
    // AArch64: メモリバリアを先に実行してからEvent Ringを読み取る
    DSB();
    ISB();

    // キャッシュインバリデート（受信前にメモリから再読み込み）
    InvalidateCache(&event_ring_[event_ring_index_], sizeof(TRB));

    // volatileポインタ経由で確実にメモリから読み取る
    volatile TRB *event_ptr = &event_ring_[event_ring_index_];
    uint32_t control = event_ptr->control;
    uint32_t status = event_ptr->status;
    uint64_t parameter = event_ptr->parameter;

    // ERDPレジスタを確認し、異常なら再設定
    static int log_count = 0; // ログ出力用カウンタ

    if (control == 0)
    {
        DSB();
        uint32_t erdp_low = ReadRtReg(0x20 + 0x18);
        uint32_t erdp_high = ReadRtReg(0x20 + 0x1C);
        uint64_t expected_erdp =
            reinterpret_cast<uint64_t>(&event_ring_[event_ring_index_]);

        /*
        if (log_count < 3)
        {
            kprintf("[xHCI DBG] PollEP: idx=%d, ctrl=0x%x, dcs=%d, cycle=%d\n",
                    event_ring_index_, control, dcs_, (control & 1));
            kprintf(
                "[xHCI DBG] rt_regs_base_=0x%lx, ERDP reg: 0x%x%08x, expected: "
                "0x%lx\n",
                rt_regs_base_, erdp_high, erdp_low, expected_erdp);
        }
        */

        // 回避策: ERDPが異常な場合、正しい値に再設定（常に実行）
        uint64_t actual_erdp =
            ((uint64_t)erdp_high << 32) | (erdp_low & ~0xFULL);
        if (actual_erdp != (expected_erdp & ~0xFULL))
        {
            if (log_count < 3)
            {
                // kprintf("[xHCI DBG] ERDP mismatch! Re-setting ERDP.\n");
            }
            uint32_t write_low = (expected_erdp & 0xFFFFFFFF) | (1 << 3);
            uint32_t write_high = expected_erdp >> 32;
            WriteRtReg(0x20 + 0x18, write_low);
            WriteRtReg(0x20 + 0x1C, write_high);
            DSB();

            // 再読み取り
            InvalidateCache(&event_ring_[event_ring_index_], sizeof(TRB));
            control = event_ptr->control;
            status = event_ptr->status;
            parameter = event_ptr->parameter;

            if (log_count < 3)
            {
                // kprintf("[xHCI DBG] After ERDP reset: ctrl=0x%x\n", control);
                log_count++;
            }
        }
    }
    else if (log_count < 3)
    {
        /*
        kprintf("[xHCI DBG] PollEP: idx=%d, ctrl=0x%x, dcs=%d, cycle=%d\n",
                event_ring_index_, control, dcs_, (control & 1));
                */
    }

    if ((control & 1) == dcs_)
    {
        AdvanceEventRing();

        int ret_code = -1;
        uint32_t trb_type = (control >> 10) & 0x3F;
        // kprintf("[xHCI DBG] Event! type=%d, status=0x%x\n", trb_type,
        // status);

        if (trb_type == 32) // Transfer Event
        {
            uint8_t event_slot = (control >> 24) & 0xFF;
            uint8_t event_dci = (control >> 16) & 0x1F;
            uint32_t comp_code = (status >> 24) & 0xFF;

            uint8_t target_dci = AddressToDCI(ep_addr);

            // kprintf("[xHCI DBG] TransferEvent: slot=%d(exp=%d), "
            //         "dci=%d(exp=%d), comp=%d\n",
            //         event_slot, slot_id, event_dci, target_dci, comp_code);

            if (event_slot == slot_id && event_dci == target_dci)
            {
                ret_code = comp_code;
            }
            else
            {
            }
        }
        else if (trb_type == 33) // Command Completion Event
        {
            uint32_t comp_code = (status >> 24) & 0xFF;
            kprintf("[xHCI DBG] CmdCompletionEvent: comp=%d\n", comp_code);
        }
        else if (trb_type == 34) // Port Status Change Event
        {
            kprintf("[xHCI DBG] PortStatusChangeEvent\n");
        }
        else
        {
            kprintf("[xHCI DBG] OtherEvent: type=%d\n", trb_type);
        }

        return ret_code;
    }

    return -1;
}

bool Controller::SendNormalTRB(uint8_t slot_id, uint8_t ep_addr, void *data_buf,
                               uint32_t len)
{
    uint8_t dci = AddressToDCI(ep_addr);
    TRB *ring = transfer_rings_[slot_id][dci];
    if (!ring)
    {
        // kprintf("[xHCI ERR] SendNormalTRB: ring is null for slot=%d,
        // dci=%d\n",
        //         slot_id, dci);
        return false;
    }

    uint32_t idx = ring_index_[slot_id][dci];
    uint8_t pcs = ring_cycle_state_[slot_id][dci];

    // IN転送（デバイス→ホスト）かOUT転送（ホスト→デバイス）かを判定
    bool is_in = (ep_addr & 0x80) != 0;

    // kprintf("[xHCI DBG] SendNormalTRB: slot=%d, ep=0x%x, dci=%d, buf=0x%lx, "
    //         "len=%u, idx=%u, pcs=%d, is_in=%d\n",
    //         slot_id, ep_addr, dci, (uint64_t)data_buf, len, idx, pcs, is_in);

    // AArch64: IN転送の場合、受信バッファを事前にインバリデート
    // これにより、DMA完了後に古いキャッシュデータを読まないようにする
    if (is_in && len > 0 && data_buf)
    {
        InvalidateCache(data_buf, len);
    }

    TRB &trb = ring[idx];
    trb.parameter = reinterpret_cast<uint64_t>(data_buf);
    trb.status = len; // Transfer Length

    // Type=1 (Normal), IOC=1 (完了時にイベント発生)
    // ISP (Interrupt on Short Packet)
    // は外してみる（IOCがあれば完了時にイベントが出るはず）
    trb.control = (pcs & 1) | (1 << 10) | (1 << 5);

    // kprintf("[xHCI DBG] TRB: param=0x%lx, status=0x%x, ctrl=0x%x\n",
    //         trb.parameter, trb.status, trb.control);

    // データバッファのダンプ（メモリ破壊確認）
    if (len >= 4 && data_buf)
    {
        uint32_t *ptr = (uint32_t *)data_buf;
        // kprintf("[xHCI DBG] Buffer Content: %08x %08x...\n", ptr[0],
        //         len >= 8 ? ptr[1] : 0);
    }

    // AArch64: OUT転送の場合のみ、送信データをフラッシュ
    if (!is_in && len > 0 && data_buf)
    {
        FlushCache(data_buf, len);
    }

    // TRBのフラッシュ
    FlushCache(&ring[idx], sizeof(TRB));

    ring_index_[slot_id][dci]++;

    if (ring_index_[slot_id][dci] == 31)
    {
        TRB &link = ring[31];
        link.parameter = reinterpret_cast<uint64_t>(ring);
        link.status = 0;
        link.control = (pcs & 1) | (6 << 10) | (1 << 1);

        // Link TRBのフラッシュ
        FlushCache(&link, sizeof(TRB));

        ring_cycle_state_[slot_id][dci] ^= 1;
        ring_index_[slot_id][dci] = 0;
        // kprintf("[xHCI DBG] Link TRB inserted, pcs toggled to %d\n",
        //         ring_cycle_state_[slot_id][dci]);
    }

    DSB();
    // kprintf("[xHCI DBG] Ringing doorbell for slot=%d, dci=%d\n", slot_id,
    // dci);
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
            kprintf("[xHCI] Found USB Legacy Support at offset %x\n",
                    current_offset);

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
                    PAUSE();
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
    DeviceContext *out_ctx = static_cast<DeviceContext *>(
        MemoryManager::Allocate(sizeof(DeviceContext), 64));
    PageManager::SetDeviceMemory(out_ctx, sizeof(DeviceContext));

    {
        uint8_t *p = reinterpret_cast<uint8_t *>(out_ctx);
        for (size_t i = 0; i < sizeof(DeviceContext); ++i)
            p[i] = 0;
    }
    FlushCache(out_ctx, sizeof(DeviceContext));

    dcbaa_[slot_id] = reinterpret_cast<uint64_t>(out_ctx);
    FlushCache(&dcbaa_[slot_id], sizeof(uint64_t));

    InputContext *input_ctx = static_cast<InputContext *>(
        MemoryManager::Allocate(sizeof(InputContext), 64));
    PageManager::SetDeviceMemory(input_ctx, sizeof(InputContext));

    {
        uint8_t *p = reinterpret_cast<uint8_t *>(input_ctx);
        for (size_t i = 0; i < sizeof(InputContext); ++i)
            p[i] = 0;
    }

    // --- Input Control Context の設定 ---
    // A0 (Slot Context) と A1 (Endpoint 0) を有効にする (Bit 0 と Bit 1)
    input_ctx->input_control_context.add_context_flags = (1 << 0) | (1 << 1);

    // --- Slot Context の設定 ---
    input_ctx->slot_context.root_hub_port_num =
        port_id; // 接続されているポート番号
    input_ctx->slot_context.route_string = 0;
    input_ctx->slot_context.context_entries = 1; // EP0まで有効
    input_ctx->slot_context.speed = speed;       // ポートから読み取った速度

    // --- Endpoint Context 0 (Control Pipe) の設定 ---
    input_ctx->ep_contexts[0].ep_type = 4; // Control Endpoint (Bidirectional)

    transfer_rings_[slot_id][1] =
        static_cast<TRB *>(MemoryManager::Allocate(sizeof(TRB) * 32, 64));
    PageManager::SetDeviceMemory(transfer_rings_[slot_id][1], sizeof(TRB) * 32);
    {
        uint8_t *p = reinterpret_cast<uint8_t *>(transfer_rings_[slot_id][1]);
        for (size_t i = 0; i < sizeof(TRB) * 32; ++i)
            p[i] = 0;
    }
    FlushCache(transfer_rings_[slot_id][1], sizeof(TRB) * 32);
    uint64_t tr_phys = reinterpret_cast<uint64_t>(transfer_rings_[slot_id][1]);
    if (speed == 4)
        input_ctx->ep_contexts[0].max_packet_size = 512;
    else if (speed == 3)
        input_ctx->ep_contexts[0].max_packet_size = 64;
    else
        input_ctx->ep_contexts[0].max_packet_size =
            8; // Low/Fullはとりあえず8 (後で読むDescriptorで確定)

    input_ctx->ep_contexts[0].max_burst_size = 0;
    input_ctx->ep_contexts[0].dequeue_pointer = tr_phys;
    input_ctx->ep_contexts[0].dequeue_pointer |= 1;
    input_ctx->ep_contexts[0].interval = 0;
    input_ctx->ep_contexts[0].average_trb_length = 8;
    input_ctx->ep_contexts[0].error_count = 3;

    // InputContext設定完了後にフラッシュ
    FlushCache(input_ctx, sizeof(InputContext));

    uint64_t command_trb_ptr =
        reinterpret_cast<uint64_t>(&command_ring_[cmd_ring_index_]);

    TRB &trb = command_ring_[cmd_ring_index_];
    trb.parameter =
        reinterpret_cast<uint64_t>(input_ctx); // Input Contextのポインタ
    trb.status = 0;
    // Type=11 (Address Device), Slot IDを設定
    trb.control = (pcs_ & 1) | (TRB_ADDRESS_DEVICE << 10) | (slot_id << 24);

    // コマンドTRBのフラッシュ
    FlushCache(&trb, sizeof(TRB));

    DSB(); // メモリバリア: コマンドTRBとInput Contextの書き込み完了を保証
    cmd_ring_index_++;
    RingDoorbell(0, 0);

    kprintf(
        "[xHCI] Sent Address Device Command (Slot %d, Speed %d). Waiting...\n",
        slot_id, speed);

    int timeout = 1000000;
    while (timeout > 0)
    {
        DSB();
        InvalidateCache(&event_ring_[event_ring_index_], sizeof(TRB));
        volatile TRB &event = event_ring_[event_ring_index_];
        uint32_t control = event.control;

        if ((control & 1) == dcs_)
        {
            AdvanceEventRing();
            uint32_t trb_type = (control >> 10) & 0x3F;
            uint64_t param = event.parameter;

            if ((trb_type == TRB_COMMAND_COMPLETION || trb_type == 33) &&
                param == command_trb_ptr)
            {
                uint8_t comp_code = (event.status >> 24) & 0xFF;
                if (comp_code == 1) // Success
                {
                    kprintf("[xHCI] Address Device Successful! Slot %d is "
                            "active.\n",
                            slot_id);
                    return true;
                }
                else
                {
                    kprintf("[xHCI] Address Device Failed. Code: %d\n",
                            comp_code);
                    return false;
                }
            }
            else
            {
                kprintf("[xHCI DBG] AddressDevice: consumed unexpected event "
                        "type=%d\n",
                        trb_type);
            }
        }
        else
        {
            timeout--;
            PAUSE();
        }
    }

    kprintf("[xHCI] TIMEOUT: Address Device Command ignored.\n");
    return false;
}

uint8_t Controller::EnableSlot()
{
    uint64_t command_trb_ptr =
        reinterpret_cast<uint64_t>(&command_ring_[cmd_ring_index_]);
    TRB &trb = command_ring_[cmd_ring_index_];
    trb.parameter = 0;
    trb.status = 0;

    // Control Field:
    // Bit 0: Cycle Bit (PCS)
    // Bit 10-15: TRB Type (Enable Slot = 9)
    // Bit 16-23: Slot Type (0)
    trb.control = (pcs_ & 1) | (TRB_ENABLE_SLOT << 10);

    cmd_ring_index_++;

    DSB();

    RingDoorbell(0, 0);

    kprintf("[xHCI] Sent Enable Slot Command. Waiting for completion...\n");
    int timeout = 10000000;
    while (timeout > 0)
    {
        uint32_t usbsts = ReadOpReg(0x04);
        if (usbsts & (1 << 2))
        {
            kprintf("\n[xHCI] FATAL ERROR: Host System Error detected! "
                    "(USBSTS=%x)\n",
                    usbsts);
            while (1)
                Hlt();
        }

        InvalidateCache(&event_ring_[event_ring_index_], sizeof(TRB));
        volatile TRB &event = event_ring_[event_ring_index_];
        uint32_t control = event.control;

        if ((control & 1) != dcs_)
        {
            timeout--;
            PAUSE();
            continue;
        }
        AdvanceEventRing();

        // TRB Type チェック (Bit 10-15)
        uint32_t trb_type = (control >> 10) & 0x3F;
        uint64_t param = event.parameter;
        if ((trb_type == TRB_COMMAND_COMPLETION || trb_type == 33) &&
            param == command_trb_ptr)
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
        else
        {
            kprintf(
                "[xHCI DBG] EnableSlot: consumed unexpected event type=%d\n",
                trb_type);
        }
        timeout--;
        PAUSE();
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
        PAUSE();
    }

    usbcmd = Read32(usbcmd_offset);
    usbcmd |= (1U << 1);
    Write32(usbcmd_offset, usbcmd);

    while (Read32(usbcmd_offset) & (1U << 1))
    {
        PAUSE();
    }

    while (Read32(usbsts_offset) & (1U << 11))
    {
        PAUSE();
    }
    kprintf("[xHCI] DEBUG: ResetController finished.\n");
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
        PAUSE();
    }

    kprintf("[xHCI] Port %d Reset Complete. Checking status...\n", port_id);

    uint32_t after = ReadOpReg(portsc_offset);
    if (after & (1 << 1))
    {
        kprintf("[xHCI] Port %d is Enabled!\n", port_id);
    }
    else
    {
        kprintf("[xHCI] Port %d Reset Failed (Not Enabled). Status: %x\n",
                port_id, after);
    }
    kprintf("[xHCI] DEBUG: ResetPort %d done.\n", port_id);
}

void Controller::ProcessInterrupt()
{
    // kprintf("[xHCI] DEBUG: ProcessInterrupt() called.\n");
    volatile TRB &event = event_ring_[event_ring_index_];
    uint32_t control = event.control;

    if ((control & 1) == dcs_)
    {
        uint32_t trb_type = (control >> 10) & 0x3F;

        // Transfer Event の場合、Event Ringのみ更新
        if (trb_type == 32)
        {
            // イベント処理は Update() に任せる
        }

        AdvanceEventRing();
    }

    // 割り込み後にUpdate()を呼び出してキーボードイベントを処理
    if (g_usb_keyboard)
    {
        g_usb_keyboard->Update();
    }
}

void Controller::AdvanceEventRing()
{
    uint32_t old_idx = event_ring_index_;
    uint8_t old_dcs = dcs_;

    event_ring_index_++;

    if (event_ring_index_ >= 32)
    {
        event_ring_index_ = 0;
        dcs_ ^= 1;
    }

    // kprintf("[xHCI DBG] AdvanceER: %d->%d, dcs: %d->%d\n", old_idx,
    //         event_ring_index_, old_dcs, dcs_);

    uint64_t erdp = reinterpret_cast<uint64_t>(&event_ring_[event_ring_index_]);
    uint32_t erdp_low_val = (erdp & 0xFFFFFFFF) | (1 << 3);
    uint32_t erdp_high_val = (erdp >> 32);

    // EHB (Bit 3) を1にして書き込むことで、Event Handler Busyをクリアする
    WriteRtReg(0x20 + 0x18, erdp_low_val);
    WriteRtReg(0x20 + 0x1C, erdp_high_val);
    DSB();

    // 書き込み確認（問題発生タイミング付近でのみ）
    if (event_ring_index_ >= 23 && event_ring_index_ <= 26)
    {
        uint32_t read_low = ReadRtReg(0x20 + 0x18);
        uint32_t read_high = ReadRtReg(0x20 + 0x1C);
        // kprintf("[xHCI DBG] ERDP write: 0x%x%08x, read: 0x%x%08x\n",
        //         erdp_high_val, erdp_low_val, read_high, read_low);
    }
}

void Controller::DebugDump() const
{
    // kprintf("[xHCI Debug] event_ring_ addr: %lx\n",
    //         reinterpret_cast<uint64_t>(event_ring_));
    // kprintf("[xHCI Debug] event_ring_index_: %d, dcs_: %d\n",
    // event_ring_index_,
    //         dcs_);

    // 現在のイベントリングエントリをダンプ
    volatile TRB &event = event_ring_[event_ring_index_];
    // kprintf(
    //     "[xHCI Debug] event.control: %x (cycle bit: %d, expected dcs_:
    //     %d)\n", event.control, event.control & 1, dcs_);
    // kprintf("[xHCI Debug] event.parameter: %lx\n", event.parameter);
    // kprintf("[xHCI Debug] event.status: %x\n", event.status);

    // ★ 追加: xHCI レジスタの状態を直接確認
    uint32_t usbsts = ReadOpReg(0x04);
    // kprintf("[xHCI Debug] USBSTS: %x (HCH=%d, HSE=%d, EINT=%d)\n", usbsts,
    //         usbsts & 1, (usbsts >> 2) & 1, (usbsts >> 3) & 1);

    // ERDP (Event Ring Dequeue Pointer) の確認
    uint32_t erdp_low = ReadRtReg(0x20 + 0x18);
    uint32_t erdp_high = ReadRtReg(0x20 + 0x1C);
    // kprintf("[xHCI Debug] ERDP: %x %x\n", erdp_high, erdp_low);

    // ERSTBA (Event Ring Segment Table Base Address) の確認
    uint32_t erstba_low = ReadRtReg(0x20 + 0x10);
    uint32_t erstba_high = ReadRtReg(0x20 + 0x14);
    // kprintf("[xHCI Debug] ERSTBA: %x %x (expected: erst_)\n", erstba_high,
    //         erstba_low);

    // Slot 1のDevice Context（Output Context）を読み取り
    if (dcbaa_ && dcbaa_[1])
    {
        InvalidateCache(reinterpret_cast<void *>(dcbaa_[1]), 1024);

        // Device ContextはSlot Context + 31 Endpoint Contexts
        // 各コンテキストは32バイト（64バイトモードの場合は64バイト）
        // EP Context 4 (dci=4, Bulk OUT) = offset 4 * 32 = 128
        uint64_t *ep4_ctx = reinterpret_cast<uint64_t *>(dcbaa_[1] + 4 * 32);

        // Dequeue Pointer はEP Context内のDW2-DW3 (offset 8-15)
        uint64_t tr_dequeue = ep4_ctx[1]; // DW2-DW3
        uint32_t ep_state =
            static_cast<uint32_t>(ep4_ctx[0] & 0x7); // DW0のbit 0-2

        // kprintf("[xHCI Debug] Slot1 EP4: State=%d, TR Dequeue=0x%lx\n",
        //         ep_state, tr_dequeue);
    }
}
} // namespace USB::XHCI