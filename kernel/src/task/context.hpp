#pragma once
#include <stdint.h>

// x86-64アーキテクチャのレジスタセット
struct alignas(16) Context
{
    uint64_t cr3; // ページテーブルベース (将来のRing3対応のため)
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t rip;    // 命令ポインタ (スイッチ時にスタックに積まれる)
    uint64_t rflags; // フラグレジスタ
    uint64_t rsp;    // スタックポインタ (将来のユーザーモード用に)
};