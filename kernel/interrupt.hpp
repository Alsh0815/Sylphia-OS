#pragma once
#include <stdint.h>

// 割り込み記述子の属性 (Type)
#define IDT_TYPE_INTERRUPT_GATE 0xE // 割り込みゲート (割り込み中、他の割り込みを禁止する)
#define IDT_TYPE_TRAP_GATE 0xF      // トラップゲート (他の割り込みを許可する)

// 割り込みフレーム構造体 (CPUがスタックに積む情報)
struct InterruptFrame
{
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

// IDTの1エントリ (16バイト)
struct InterruptDescriptor
{
    uint16_t offset_low;       // ハンドラ関数のアドレス (下位16bit)
    uint16_t segment_selector; // コードセグメント (CS) のセレクタ値

    // ビットフィールド
    uint16_t ist : 3;        // Interrupt Stack Table (通常は0)
    uint16_t reserved_1 : 5; // 予約 (0)
    uint16_t type : 4;       // ゲートタイプ (0xE or 0xF)
    uint16_t reserved_2 : 1; // 0
    uint16_t dpl : 2;        // Descriptor Privilege Level (通常0)
    uint16_t present : 1;    // 有効フラグ (1で有効)

    uint16_t offset_middle; // ハンドラ関数のアドレス (中位16bit)
    uint32_t offset_high;   // ハンドラ関数のアドレス (上位32bit)
    uint32_t reserved_3;    // 予約 (0)
} __attribute__((packed));

// IDTをセットアップする関数
void SetupInterrupts();
void SetIDTEntry(int index, uint64_t offset, uint16_t selector, uint16_t type);