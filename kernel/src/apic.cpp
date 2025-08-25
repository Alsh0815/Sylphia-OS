#include <stdint.h>
#include "apic.hpp"
#include "paging.hpp"
#include "pic.hpp"

namespace
{
    // LAPICレジスタのオフセット (32-bit)
    volatile uint32_t *lapic_regs;
    volatile uint32_t *ioapic_addr;

    constexpr uint64_t IOAPIC_DEFAULT_PHYS_ADDR = 0xFEC00000;
    constexpr uint32_t LAPIC_REG_ID = 0x0020;          // LAPIC ID Register
    constexpr uint32_t LAPIC_REG_SIVR = 0x00F0;        // Spurious Interrupt Vector Register
    constexpr uint32_t LAPIC_REG_EOI = 0x00B0;         // EOI Register
    constexpr uint32_t LAPIC_REG_LVT_TMR = 0x0320;     // LVT Timer Register
    constexpr uint32_t LAPIC_REG_TMR_INITCNT = 0x0380; // Timer Initial Count Register
    constexpr uint32_t LAPIC_REG_TMR_CURCNT = 0x0390;  // Timer Current Count Register
    constexpr uint32_t LAPIC_REG_TMR_DIV = 0x03E0;     // Timer Divide Configuration Register

    // MSRからLAPICのベース物理アドレスを読み出す関数
    uint64_t get_lapic_base()
    {
        uint32_t lo, hi;
        asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x1B)); // IA32_APIC_BASE MSR
        return ((uint64_t)hi << 32) | lo;
    }

    uint32_t read_reg(uint8_t reg_offset)
    {
        ioapic_addr[0] = reg_offset;
        return ioapic_addr[4];
    }
    void write_reg(uint8_t reg_offset, uint32_t value)
    {
        ioapic_addr[0] = reg_offset;
        ioapic_addr[4] = value;
    }
}

void apic_eoi()
{
    if (lapic_regs)
    {
        lapic_regs[LAPIC_REG_EOI / 4] = 0;
    }
}

void initialize_apic()
{
    // --- LAPICのベースアドレスを取得し、仮想アドレスにマップ ---
    const uint64_t lapic_base_phys = get_lapic_base() & ~0xFFFULL;
    if (!paging::map_mmio_range(lapic_base_phys, 0x1000))
    {
        return;
    }
    lapic_regs = (uint32_t *)lapic_base_phys; // 恒等マップなので物理アドレスをそのまま使える

    // --- レガシーPICを無効化 ---
    disable_pic();

    // --- LAPIC自体を有効化 ---
    // Spurious Interrupt Vector Registerの下位8bitを1にすると有効になる
    // 0xFFは「不正な割り込み」用のベクタ。ここでは有効化フラグ(bit 8)を立てるのが目的
    lapic_regs[LAPIC_REG_SIVR / 4] = 0x1FF;

    // --- タイマーの設定 ---
    lapic_regs[LAPIC_REG_TMR_DIV / 4] = 0b0011;

    // 割り込みの設定 (LVT Timer Register)
    constexpr uint32_t timer_vector = 0x40;
    constexpr uint32_t timer_mode_periodic = (1 << 17);
    lapic_regs[LAPIC_REG_LVT_TMR / 4] = timer_vector | timer_mode_periodic;

    // カウンタの初期値を設定 (この値が大きいほど割り込み間隔は長くなる)
    // この値は環境に依存するため調整が必要だが、まずは固定値で試す
    lapic_regs[LAPIC_REG_TMR_INITCNT / 4] = 10000000; // 10,000,000
}

void initialize_ioapic()
{
    // I/O APICのアドレスをマップする処理 ...
    if (!paging::map_mmio_range(IOAPIC_DEFAULT_PHYS_ADDR, 0x1000))
    {
        // マッピングに失敗した場合は処理を中断
        return;
    }
    // 恒等マッピングなので物理アドレスをそのまま仮想アドレスとして使える
    ioapic_addr = (uint32_t *)IOAPIC_DEFAULT_PHYS_ADDR;

    // --- PS/2マウス (IRQ 12) の転送ルールを設定 ---
    const uint8_t irq_mouse = 12;
    const uint8_t mouse_vector = 0x2C; // idt.cppで設定したベクタ番号

    // リダイレクションテーブルのレジスタオフセットを計算
    uint8_t reg_offset = 0x10 + irq_mouse * 2;

    // 下位32bit: ベクタ番号などを設定
    uint32_t lower_word = mouse_vector; // Bit 0-7: Vector
                                        // Bit 15 (Trigger Mode): 0 = Edge
                                        // Bit 16 (Mask): 0 = unmask
    write_reg(reg_offset, lower_word);

    // 上位32bit: 配送先のCPUコア(LAPIC ID)を設定
    uint32_t upper_word = (0 << 24); // Destination ID = 0 (最初のコア)
    write_reg(reg_offset + 1, upper_word);
}