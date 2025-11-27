#pragma once
#include <stdint.h>
#include "x86_descriptor.hpp"

// セグメントセレクタの定義
// 下位3ビットは RPL(Request Privilege Level) と TI(Table Indicator)
// Index:
// 0: Null
// 1: Kernel Code (0x08)
// 2: Kernel Data (0x10)
// 3: User Data 32 (未使用)
// 4: User Data 64 (0x20 | 3 = 0x23)
// 5: User Code 64 (0x28 | 3 = 0x2B)
// 6: TSS (0x30)
const uint16_t kKernelCS = 1 << 3;
const uint16_t kKernelDS = 2 << 3;
const uint16_t kUserDS = (4 << 3) | 3; // RPL=3
const uint16_t kUserCS = (5 << 3) | 3; // RPL=3
const uint16_t kTSS = 6 << 3;

// TSS (Task State Segment) 64-bit Structure
struct TSS64
{
    uint32_t reserved1;
    uint64_t rsp0; // Ring 0 に戻るときに使われるスタックポインタ
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved2;
    uint64_t ist[7]; // Interrupt Stack Table
    uint64_t reserved3;
    uint16_t reserved4;
    uint16_t iomap_base;
} __attribute__((packed));

void SetupSegments();

void SetKernelStack(uint64_t stack_addr);