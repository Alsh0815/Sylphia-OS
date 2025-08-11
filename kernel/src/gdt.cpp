#include "gdt.hpp"
#include "kernel_runtime.hpp"

namespace
{

    struct __attribute__((packed)) GdtPtr
    {
        uint16_t limit;
        uint64_t base;
    };
    struct __attribute__((packed)) SysSegDesc64
    {
        uint16_t limit0;
        uint16_t base0;
        uint8_t base1;
        uint8_t type; // 0x89 = present|DPL0|type=9 (64bit TSS)
        uint8_t limit1 : 4;
        uint8_t flags : 4; // 0
        uint8_t base2;
        uint32_t base3;
        uint32_t rsvd;
    };

    alignas(16) static uint64_t g_gdt[7]; // [0]null,[1]code64,[2]data,[3..4]TSS
    alignas(16) static Tss64 g_tss;
    static GdtPtr g_gdtr;
    static uint16_t g_cs_sel = 0x08; // 後で far jmp で載せ替える

    inline uint16_t make_sel(int index, int ti = 0, int rpl = 0) { return (index << 3) | (ti << 2) | rpl; }

} // anon

namespace gdt
{

    uint16_t cs() { return g_cs_sel; }

    bool init(uint64_t ist1_top)
    {
        // --- GDT エントリ作成 ---
        // 0: null
        g_gdt[0] = 0;

        // 1: 64bit Code (0x00AF9A000000FFFF 相当だが limit無視でOK)
        //    access=0x9A (P=1,DPL=0,Code=1,R=1), flags=0xA (L=1,G=1)
        uint64_t code = 0;
        code |= (uint64_t)0x0000FFFF;       // limit low
        code |= (uint64_t)0x00000000 << 16; // base low
        code |= (uint64_t)0x9A << 40;       // access
        code |= (uint64_t)0xA << 52;        // flags (L|G)
        g_gdt[1] = code;

        // 2: Data (RW), 64bitでは意味薄いが互換
        uint64_t data = 0;
        data |= (uint64_t)0x0000FFFF;
        data |= (uint64_t)0x00000000 << 16;
        data |= (uint64_t)0x92 << 40; // access RW data
        data |= (uint64_t)0xC << 52;  // flags (G=1) 互換
        g_gdt[2] = data;

        // 3-4: TSS (システムディスクリプタは 16バイト使用)
        uint64_t tss_base = (uint64_t)&g_tss;
        uint32_t tss_lim = sizeof(Tss64) - 1;
        SysSegDesc64 tssd = {};
        tssd.limit0 = tss_lim & 0xFFFF;
        tssd.base0 = tss_base & 0xFFFF;
        tssd.base1 = (tss_base >> 16) & 0xFF;
        tssd.type = 0x89; // present | type=9 (64bit TSS)
        tssd.limit1 = (tss_lim >> 16) & 0xF;
        tssd.flags = 0;
        tssd.base2 = (tss_base >> 24) & 0xFF;
        tssd.base3 = (tss_base >> 32);
        // g_gdt[3], g_gdt[4] に 16B を詰め込む
        static_assert(sizeof(SysSegDesc64) == 16, "TSS desc size");
        memcpy(&g_gdt[3], &tssd, sizeof(tssd));

        // --- TSS 初期化 (IST1 だけ設定) ---
        memset(&g_tss, 0, sizeof(g_tss));
        g_tss.ist1 = ist1_top; // 高アドレスをセット（下向き成長）

        g_tss.io_map_base = sizeof(Tss64); // I/O マップなし

        // --- LGDT & セグメント切替 ---
        g_gdtr.limit = sizeof(g_gdt) - 1;
        g_gdtr.base = (uint64_t)g_gdt;
        asm volatile("lgdt %0" ::"m"(g_gdtr) : "memory");

        uint16_t cs_sel = make_sel(1); // 0x08
        uint16_t ds_sel = make_sel(2); // 0x10

        // CS は far jump で更新、他は mov で0/データに
        asm volatile(
            "pushq %0\n\t"
            "leaq 1f(%%rip), %%rax\n\t"
            "pushq %%rax\n\t"
            "lretq\n\t" // far return: CS=cs_sel, RIP=1f
            "1:\n\t"
            :
            : "r"((uint64_t)cs_sel)
            : "rax", "memory");
        g_cs_sel = cs_sel;

        asm volatile("mov %0, %%ds\n\tmov %0, %%es\n\tmov %0, %%ss\n\t" ::"r"(ds_sel) : "memory");

        // --- LTR (TSS ロード) ---
        uint16_t tss_sel = make_sel(3); // TSS desc のインデックス=3（16B占有）
        asm volatile("ltr %0" ::"r"(tss_sel) : "memory");

        return true;
    }

} // namespace gdt
