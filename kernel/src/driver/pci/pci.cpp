#include "pci.hpp"
#include "../../io.hpp"
#include "../../console.hpp"

namespace pci
{

    static inline uint32_t cfg_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off)
    {
        // 31:Enable, 23:16 Bus, 15:11 Device, 10:8 Function, 7:2 Register, 1:0 must be 0
        return (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)func << 8) | (off & 0xFC);
    }

    uint32_t read_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
    {
        outl(0xCF8, cfg_addr(bus, dev, func, offset));
        return inl(0xCFC);
    }

    void write_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val)
    {
        outl(0xCF8, cfg_addr(bus, dev, func, offset));
        outl(0xCFC, val);
    }

    static void read_basic(uint8_t b, uint8_t d, uint8_t f, Device &info)
    {
        uint32_t v0 = read_config32(b, d, f, 0x00); // [15:0] vendor, [31:16] device
        info.vendor = (uint16_t)(v0 & 0xFFFF);
        info.device = (uint16_t)(v0 >> 16);

        uint32_t v8 = read_config32(b, d, f, 0x08); // class/subclass/progif/revid
        info.rev_id = (uint8_t)(v8 & 0xFF);
        info.prog_if = (uint8_t)((v8 >> 8) & 0xFF);
        info.subclass = (uint8_t)((v8 >> 16) & 0xFF);
        info.class_code = (uint8_t)((v8 >> 24) & 0xFF);

        uint32_t vc = read_config32(b, d, f, 0x0C); // header type in [23:16]
        info.header_type = (uint8_t)((vc >> 16) & 0xFF);

        info.bus = b;
        info.dev = d;
        info.func = f;
        for (int i = 0; i < 6; i++)
            info.bar[i] = 0;
    }

    // 64bit/32bit BAR を正規化して読む（サイズは検出しない。値だけ）
    // 戻り値: 次に読むBARインデックス（64bitなら +2、32bit/IOや空なら +1）
    static int read_bar(uint8_t b, uint8_t d, uint8_t f, int idx, uint64_t &out)
    {
        uint32_t lo = read_config32(b, d, f, 0x10 + idx * 4);
        if (lo == 0)
        {
            out = 0;
            return 1;
        }

        if (lo & 0x1)
        {
            // I/O BAR（今回は使わない）
            out = (uint64_t)(lo & ~0x3u);
            return 1;
        }
        else
        {
            // Memory BAR
            uint32_t type = (lo >> 1) & 0x3; // 0=32bit, 2=64bit
            uint64_t base_lo = (uint64_t)(lo & ~0xFULL);
            if (type == 2)
            {
                uint32_t hi = read_config32(b, d, f, 0x10 + (idx + 1) * 4);
                out = (((uint64_t)hi << 32) | base_lo);
                return 2; // 64bitは次のBARを消費
            }
            else
            {
                out = base_lo;
                return 1;
            }
        }
    }

    static void read_bars(Device &dev)
    {
        int i = 0;
        while (i < 6)
        {
            uint64_t addr = 0;
            int step = read_bar(dev.bus, dev.dev, dev.func, i, addr);
            dev.bar[i] = addr;
            if (step == 2 && i + 1 < 6)
                dev.bar[i + 1] = 0; // 上位は格納済み扱い
            i += step;
        }
    }

    void enable_mem_busmaster(const Device &d)
    {
        uint32_t cmdsts = read_config32(d.bus, d.dev, d.func, 0x04); // command/status
        uint16_t cmd = (uint16_t)(cmdsts & 0xFFFF);
        cmd |= (1u << 1); // Memory Space Enable
        cmd |= (1u << 2); // Bus Master Enable
        cmdsts = (cmdsts & 0xFFFF0000u) | cmd;
        write_config32(d.bus, d.dev, d.func, 0x04, cmdsts);
    }

    bool scan_nvme(Console &con, Device &out)
    {
        bool found = false;
        for (uint8_t bus = 0; bus < 256; ++bus)
        {
            for (uint8_t dev = 0; dev < 32; ++dev)
            {
                // function 0 を読んで存在確認
                uint32_t v0 = read_config32(bus, dev, 0, 0x00);
                if ((v0 & 0xFFFF) == 0xFFFF)
                    continue; // no device

                // function 0 の header type
                uint32_t vc = read_config32(bus, dev, 0, 0x0C);
                uint8_t hdr = (uint8_t)((vc >> 16) & 0xFF);
                uint8_t fn_max = (hdr & 0x80) ? 8 : 1;

                for (uint8_t func = 0; func < fn_max; ++func)
                {
                    uint32_t vv = read_config32(bus, dev, func, 0x00);
                    if ((vv & 0xFFFF) == 0xFFFF)
                        continue;

                    Device info{};
                    read_basic(bus, dev, func, info);
                    read_bars(info);

                    // ログ（簡潔）
                    /*
                    con.printf("PCI %02x:%02x.%u ven=%04x dev=%04x class=%02x.%02x.%02x\n",
                               bus, dev, func, info.vendor, info.device, info.class_code, info.subclass, info.prog_if);
                    */

                    // NVMe?  class=0x01, subclass=0x08, prog_if=0x02
                    if (info.class_code == 0x01 && info.subclass == 0x08 && info.prog_if == 0x02)
                    {
                        read_bars(info);

                        con.printf("NVMe %02x:%02x.%u ven=%04x dev=%04x\n",
                                   bus, dev, func, info.vendor, info.device);
                        for (int i = 0; i < 6; i++)
                            if (info.bar[i])
                                con.printf("  BAR%d = 0x%p\n", i, (void *)info.bar[i]);

                        enable_mem_busmaster(info);
                        con.println("  MEM+BusMaster enabled.");

                        out = info;
                        return true; // ★ 見つけたら即終了
                    }
                }
            }
        }
        if (!found)
            con.println("No NVMe device found on PCI.");
        return found;
    }

} // namespace pci
