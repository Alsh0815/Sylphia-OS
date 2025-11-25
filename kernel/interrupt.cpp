#include "interrupt.hpp"
#include <stdint.h>

// IDTの実体 (256個の割り込みに対応)
InterruptDescriptor idt[256];

// IDTR (lidt命令に渡す構造体)
struct IDTR
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// 指定した番号の割り込みにハンドラ関数を登録するヘルパー
// (今回はまだハンドラがないので使いませんが、枠組みとして作ります)
void SetIDTEntry(int index, uint64_t offset, uint16_t selector, uint16_t type)
{
    idt[index].offset_low = offset & 0xFFFF;
    idt[index].offset_middle = (offset >> 16) & 0xFFFF;
    idt[index].offset_high = (offset >> 32) & 0xFFFFFFFF;

    idt[index].segment_selector = selector;
    idt[index].ist = 0;
    idt[index].type = type;
    idt[index].dpl = 0;     // カーネル権限
    idt[index].present = 1; // 有効化
    idt[index].reserved_1 = 0;
    idt[index].reserved_2 = 0;
    idt[index].reserved_3 = 0;
}

// IDTをCPUにロードする
void LoadIDT(uint16_t limit, uint64_t base)
{
    IDTR idtr;
    idtr.limit = limit;
    idtr.base = base;
    __asm__ volatile("lidt %0" ::"m"(idtr));
}

void SetupInterrupts()
{
    // まずはIDTを空(無効)の状態で初期化
    // (グローバル変数は0初期化されるので明示しなくても良いですが念の為)
    // ここでループして初期化もできますが、Present=0ならCPUは無視するのでOK

    // 本来はここで SetIDTEntry(...) を呼んでハンドラを登録します
    // 次回以降、ここにキーボードハンドラなどを登録します

    // IDTをロード
    LoadIDT(sizeof(idt) - 1, (uint64_t)&idt[0]);
}