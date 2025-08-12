#pragma once
#include <stdint.h>
#include <stddef.h>

namespace heap
{

    // 初期化: bytes 分の連続領域を PMM から確保してヒープにする（固定長・拡張なし）
    bool init(uint64_t initial_bytes = 256ull * 1024);

    // 拡張チャンクの既定サイズを変更（任意）
    void set_chunk_size(uint64_t bytes);

    // 最小API
    void *kmalloc(size_t size, size_t align = 16, bool zero = false);
    void kfree(void *p);                      // bump版では no-op
    void *krealloc(void *p, size_t new_size); // bump版では「新規確保→旧は放置」

    uint64_t capacity(); // 総容量
    uint64_t used();     // 使用済み
    uint64_t remain();   // 残り

} // namespace heap
