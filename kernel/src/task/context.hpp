#pragma once
#include <stdint.h>

// x86-64アーキテクチャのレジスタセット
struct alignas(16) Context
{
    uint64_t cr3, rip, rflags, reserved1;            // offset 0
    uint64_t cs, ss, fs, gs;                         // offset 32
    uint64_t rax, rbx, rcx, rdx, rdi, rsi, rsp, rbp; // offset 64
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;   // offset 128
    char fxsave_area[512];                           // offset 192
};