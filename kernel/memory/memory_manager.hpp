#pragma once
#include <stddef.h>
#include <stdint.h>
#include "memory/memory.hpp"

class MemoryManager
{
public:
    // メモリマップを受け取り、最大の空き領域をヒープとして登録する
    static void Initialize(const MemoryMap &memmap);

    // 指定バイト数を確保してポインタを返す
    static void *Allocate(size_t size);

    // メモリを解放する (今回は簡易実装のため何もしない)
    static void Free(void *ptr);

private:
    // ヒープ領域の管理変数
    static uintptr_t heap_start_;  // 開始アドレス
    static uintptr_t heap_end_;    // 終了アドレス
    static uintptr_t current_pos_; // 現在どこまで使ったか
};