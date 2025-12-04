#include "pci/pci.hpp"
#include "driver/usb/xhci.hpp"
#include "io.hpp"
#include "printk.hpp"


namespace PCI
{

// コンフィギュレーションアドレスを作成するヘルパー
// Enable Bit(31) | Bus(23-16) | Device(15-11) | Function(10-8) | Register(7-2)
uint32_t MakeAddress(uint8_t bus, uint8_t device, uint8_t function,
                     uint8_t reg_addr)
{
    return (1U << 31) | (static_cast<uint32_t>(bus) << 16) |
           (static_cast<uint32_t>(device) << 11) |
           (static_cast<uint32_t>(function) << 8) | (reg_addr & 0xFC);
}

uintptr_t ReadBar0(const Device &dev)
{
    // BAR0 はレジスタオフセット 0x10
    uint32_t bar0 = ReadConfReg(dev, 0x10);

    // 下位4ビットはフラグなのでマスクする
    // bit 0: Memory Space Indicator (0=Memory, 1=I/O)
    // bit 1-2: Type (00=32bit, 10=64bit)
    // bit 3: Prefetchable

    // 64bit BARかどうかチェック (Type == 2)
    uint32_t type = (bar0 >> 1) & 0x03;

    if (type == 0x02)
    { // 64bit
        // BAR1 (オフセット 0x14) から上位32bitを読む
        uint32_t bar1 = ReadConfReg(dev, 0x14);

        // 結合して返す (下位4bitのフラグは消す: & ~0xF)
        uintptr_t addr = (static_cast<uintptr_t>(bar1) << 32) | (bar0 & ~0xF);
        return addr;
    }
    else
    {
        // 32bit
        return static_cast<uintptr_t>(bar0 & ~0xF);
    }
}

uint32_t ReadConfReg(const Device &dev, uint8_t reg_addr)
{
    IoOut32(kConfigAddress,
            MakeAddress(dev.bus, dev.device, dev.function, reg_addr));
    return IoIn32(kConfigData);
}

void WriteConfReg(const Device &dev, uint8_t reg_addr, uint32_t value)
{
    IoOut32(kConfigAddress,
            MakeAddress(dev.bus, dev.device, dev.function, reg_addr));
    IoOut32(kConfigData, value);
}

// デバイスを追加する (今回は表示するだけ)
void AddDevice(uint8_t bus, uint8_t device, uint8_t function,
               uint16_t vendor_id)
{
    Device dev = {bus, device, function, vendor_id, 0, 0, 0, 0};

    // レジスタ0x00: Device ID (上位16bit) | Vendor ID (下位16bit)
    uint32_t reg0 = ReadConfReg(dev, 0x00);
    dev.device_id = (reg0 >> 16);

    // レジスタ0x08: Class Code, Sub Class, Prog IF, Revision ID
    uint32_t reg8 = ReadConfReg(dev, 0x08);
    dev.base_class = (reg8 >> 24) & 0xFF;
    dev.sub_class = (reg8 >> 16) & 0xFF;
    dev.prog_if = (reg8 >> 8) & 0xFF;

    kprintf("PCI %d:%d.%d : Vend=%x Dev=%x Class=%x Sub=%x", bus, device,
            function, dev.vendor_id, dev.device_id, dev.base_class,
            dev.sub_class);

    // 有名なクラスコードの注釈
    if (dev.base_class == 0x01 && dev.sub_class == 0x08)
    {
        kprintf(" [NVMe Controller]");
    }
    else if (dev.base_class == 0x0C && dev.sub_class == 0x03)
    {
        kprintf(" [USB Controller]");
    }
    else if (dev.base_class == 0x03)
    {
        kprintf(" [Graphics]");
    }
    else if (dev.base_class == 0x02)
    {
        kprintf(" [Network]");
    }

    kprintf("\n");
}

void ScanAllBus()
{
    kprintf("Scanning PCI Bus...\n");

    // バス 0〜255
    for (int bus = 0; bus < 256; ++bus)
    {
        // デバイス 0〜31
        for (int dev = 0; dev < 32; ++dev)
        {
            // ファンクション 0〜7
            for (int func = 0; func < 8; ++func)
            {

                // Vendor IDを確認 (レジスタオフセット0の下位16bit)
                Device temp_dev = {static_cast<uint8_t>(bus),
                                   static_cast<uint8_t>(dev),
                                   static_cast<uint8_t>(func)};
                uint32_t reg0 = ReadConfReg(temp_dev, 0x00);
                uint16_t vendor_id = reg0 & 0xFFFF;

                // Vendor IDが 0xFFFF ならデバイス未接続
                if (vendor_id != 0xFFFF)
                {
                    AddDevice(bus, dev, func, vendor_id);
                }
            }
        }
    }
    kprintf("PCI Scan Done.\n");
}

bool SetupMSI(const Device &dev, uint8_t vector)
{
    kprintf("[PCI MSI] Setting up MSI for device %d:%d.%d with vector 0x%x\n",
            dev.bus, dev.device, dev.function, vector);

    // Status Register (0x06) でCapability List存在を確認
    uint32_t status_cmd = ReadConfReg(dev, 0x04);
    uint16_t status = (status_cmd >> 16) & 0xFFFF;

    if (!(status & (1 << 4))) // Bit 4: Capabilities List
    {
        kprintf("[PCI MSI] Device does not support Capabilities List\n");
        return false;
    }

    // Capabilities Pointer (0x34) を読む
    uint32_t cap_ptr_reg = ReadConfReg(dev, 0x34);
    uint8_t cap_ptr = cap_ptr_reg & 0xFF;

    // Capability Listを走査
    while (cap_ptr != 0)
    {
        uint32_t cap_reg = ReadConfReg(dev, cap_ptr);
        uint8_t cap_id = cap_reg & 0xFF;
        uint8_t next_ptr = (cap_reg >> 8) & 0xFF;

        // MSI-X (0x11) をまず探す
        if (cap_id == 0x11)
        {
            kprintf("[PCI MSI] Found MSI-X capability at offset 0x%x\n",
                    cap_ptr);

            // MSI-X Message Control (cap_ptr + 0x02)
            uint32_t msg_ctrl_reg = ReadConfReg(dev, cap_ptr);
            uint16_t msg_ctrl = (msg_ctrl_reg >> 16) & 0xFFFF;

            // MSI-X Enable (bit 15) と Function Mask (bit 14)
            msg_ctrl |= (1 << 15);  // MSI-X Enable
            msg_ctrl &= ~(1 << 14); // Clear Function Mask

            // Message Control を書き戻し
            uint32_t new_ctrl_reg = (cap_reg & 0xFFFF) | (msg_ctrl << 16);
            WriteConfReg(dev, cap_ptr, new_ctrl_reg);

            kprintf(
                "[PCI MSI] MSI-X enabled successfully (simplified setup)\n");
            return true;
        }
        // MSI (0x05)
        else if (cap_id == 0x05)
        {
            kprintf("[PCI MSI] Found MSI capability at offset 0x%x\n", cap_ptr);

            // MSI Message Control (cap_ptr + 0x02)
            uint32_t msg_ctrl_reg = ReadConfReg(dev, cap_ptr);
            uint16_t msg_ctrl = (msg_ctrl_reg >> 16) & 0xFFFF;
            bool is_64bit = (msg_ctrl & (1 << 7)) != 0;

            kprintf("[PCI MSI] MSI is %s\n", is_64bit ? "64-bit" : "32-bit");

            // MSI Address (LAPIC base address)
            // Local APIC default address: 0xFEE00000
            // Format: 0xFEE + Destination ID (8bit) + Reserved (12bit)
            uint32_t msi_address = 0xFEE00000;
            WriteConfReg(dev, cap_ptr + 0x04, msi_address);

            if (is_64bit)
            {
                WriteConfReg(dev, cap_ptr + 0x08, 0); // Upper 32-bit = 0
                // MSI Data at offset 0x0C for 64-bit
                WriteConfReg(dev, cap_ptr + 0x0C, vector);
            }
            else
            {
                // MSI Data at offset 0x08 for 32-bit
                WriteConfReg(dev, cap_ptr + 0x08, vector);
            }

            // MSI Enable (bit 0 of Message Control)
            msg_ctrl |= (1 << 0);
            uint32_t new_ctrl_reg = (cap_reg & 0xFFFF) | (msg_ctrl << 16);
            WriteConfReg(dev, cap_ptr, new_ctrl_reg);

            kprintf("[PCI MSI] MSI enabled successfully with vector 0x%x\n",
                    vector);
            return true;
        }

        cap_ptr = next_ptr;
    }

    kprintf("[PCI MSI] No MSI or MSI-X capability found\n");
    return false;
}

void SetupPCI()
{
    kprintf("Setting up PCI...\n");
    for (int bus = 0; bus < 256; ++bus)
    {
        for (int dev = 0; dev < 32; ++dev)
        {
            for (int func = 0; func < 8; ++func)
            {
                PCI::Device d = {static_cast<uint8_t>(bus),
                                 static_cast<uint8_t>(dev),
                                 static_cast<uint8_t>(func)};
                uint16_t vendor = PCI::ReadConfReg(d, 0x00) & 0xFFFF;

                if (vendor == 0xFFFF)
                    continue;

                uint32_t reg8 = PCI::ReadConfReg(d, 0x08);
                uint8_t base = (reg8 >> 24) & 0xFF;
                uint8_t sub = (reg8 >> 16) & 0xFF;
                uint8_t prog_if = (reg8 >> 8) & 0xFF;

                // USB xHCI Controller
                // Base=0x0C (Serial Bus), Sub=0x03 (USB), Prog_IF=0x30 (xHCI)
                if (base == 0x0C && sub == 0x03 && prog_if == 0x30)
                {
                    kprintf("Found xHCI Controller!\n");
                    PCI::Device *xhci_dev = nullptr;
                    PCI::Device found_dev;
                    found_dev = d;
                    xhci_dev = &found_dev;
                    g_xhci = new USB::XHCI::Controller(*xhci_dev);
                    g_xhci->Initialize();
                }
            }
        }
    }
    kprintf("PCI Setup Complete.\n");
}
} // namespace PCI