#include "driver/usb/xhci.hpp"
#include "pci/pci.hpp"
#include "io.hpp"
#include "printk.hpp"

namespace PCI
{

    // コンフィギュレーションアドレスを作成するヘルパー
    // Enable Bit(31) | Bus(23-16) | Device(15-11) | Function(10-8) | Register(7-2)
    uint32_t MakeAddress(uint8_t bus, uint8_t device, uint8_t function, uint8_t reg_addr)
    {
        return (1U << 31) | (static_cast<uint32_t>(bus) << 16) | (static_cast<uint32_t>(device) << 11) | (static_cast<uint32_t>(function) << 8) | (reg_addr & 0xFC);
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
        IoOut32(kConfigAddress, MakeAddress(dev.bus, dev.device, dev.function, reg_addr));
        return IoIn32(kConfigData);
    }

    void WriteConfReg(const Device &dev, uint8_t reg_addr, uint32_t value)
    {
        IoOut32(kConfigAddress, MakeAddress(dev.bus, dev.device, dev.function, reg_addr));
        IoOut32(kConfigData, value);
    }

    // デバイスを追加する (今回は表示するだけ)
    void AddDevice(uint8_t bus, uint8_t device, uint8_t function, uint16_t vendor_id)
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

        kprintf("PCI %d:%d.%d : Vend=%x Dev=%x Class=%x Sub=%x",
                bus, device, function, dev.vendor_id, dev.device_id, dev.base_class, dev.sub_class);

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
                    Device temp_dev = {static_cast<uint8_t>(bus), static_cast<uint8_t>(dev), static_cast<uint8_t>(func)};
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

    void SetupPCI()
    {
        kprintf("Setting up PCI...\n");
        for (int bus = 0; bus < 256; ++bus)
        {
            for (int dev = 0; dev < 32; ++dev)
            {
                for (int func = 0; func < 8; ++func)
                {
                    PCI::Device d = {static_cast<uint8_t>(bus), static_cast<uint8_t>(dev), static_cast<uint8_t>(func)};
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
}