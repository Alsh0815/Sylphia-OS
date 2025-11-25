#pragma once
#include <stdint.h>

// Local APICのレジスタオフセット
#define LAPIC_ID 0x020
#define LAPIC_VER 0x030
#define LAPIC_TPR 0x080       // Task Priority
#define LAPIC_EOI 0x0B0       // End of Interrupt
#define LAPIC_SVR 0x0F0       // Spurious Interrupt Vector
#define LAPIC_ICR_LOW 0x300   // Interrupt Command Register Low
#define LAPIC_ICR_HIGH 0x310  // Interrupt Command Register High
#define LAPIC_LVT_TIMER 0x320 // LVT Timer

class LocalAPIC
{
public:
    LocalAPIC();

    // Local APICを有効化する
    void Enable();

    // 割り込み処理の完了をCPUに通知する (これを呼ばないと次の割り込みが来ない)
    void EndOfInterrupt();

    // APIC ID (CPU Core ID) を取得
    uint32_t GetID();

private:
    // レジスタ読み書き用
    uint32_t Read(uint32_t register_offset);
    void Write(uint32_t register_offset, uint32_t value);
};

// グローバルインスタンス
extern LocalAPIC *g_lapic;