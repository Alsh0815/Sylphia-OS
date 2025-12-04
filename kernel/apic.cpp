#include "apic.hpp"

// 通常、Local APICは物理アドレス 0xFEE00000 にある
// 本来は MSR (Model Specific Register) IA32_APIC_BASE を読んで確認すべきだが、
// ほとんどのPCで固定なので今回は定数定義で進める
const uintptr_t kLocalApicBase = 0xFEE00000;

LocalAPIC *g_lapic = nullptr;

LocalAPIC::LocalAPIC() {}

#define LAPIC_TIMER_DIV 0x3E0     // Timer Divide Configuration
#define LAPIC_TIMER_INIT 0x380    // Timer Initial Count
#define LAPIC_TIMER_CURRENT 0x390 // Timer Current Count
void LocalAPIC::StartTimer(uint32_t interval_ms, uint8_t vector)
{
    // Timer Divide Configuration (divide by 16)
    Write(LAPIC_TIMER_DIV, 0x03);
    // LVT Timer Register: Periodic mode + 割り込みベクタ
    // Bit 17: Timer Mode (0=One-shot, 1=Periodic)
    Write(LAPIC_LVT_TIMER, (1 << 17) | vector);
    // Initial Count (周波数依存、簡易計算)
    // 仮にAPIC Timerが1GHzとして、16分周で62.5MHz
    // 1msあたり62500カウント
    uint32_t count = 62500 * interval_ms;
    Write(LAPIC_TIMER_INIT, count);
}

uint32_t LocalAPIC::Read(uint32_t register_offset)
{
    // メモリマップドI/O (MMIO) なので、ポインタとしてアクセス
    volatile uint32_t *reg =
        reinterpret_cast<volatile uint32_t *>(kLocalApicBase + register_offset);
    return *reg;
}

void LocalAPIC::Write(uint32_t register_offset, uint32_t value)
{
    volatile uint32_t *reg =
        reinterpret_cast<volatile uint32_t *>(kLocalApicBase + register_offset);
    *reg = value;
}

void LocalAPIC::Enable()
{
    // 1. Spurious Interrupt Vector Register (SVR) の設定
    // 下位8bit: スプリアス割り込みのベクタ番号 (0xFFにしておくのが定石)
    // 8bit目(0x100): APIC Software Enable フラグ
    Write(LAPIC_SVR, 0x100 | 0xFF);
}

void LocalAPIC::EndOfInterrupt()
{
    // EOIレジスタに0を書き込むと完了通知になる
    Write(LAPIC_EOI, 0);
}

uint32_t LocalAPIC::GetID()
{
    // IDレジスタの24-31bitがAPIC ID
    return Read(LAPIC_ID) >> 24;
}