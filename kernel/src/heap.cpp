#include "heap.hpp"
#include "pmm.hpp"
#include <stdint.h>

namespace
{
    constexpr uint64_t PAGE = 4096;

    inline uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

    static uint8_t *g_base = nullptr; // 物理=仮想（恒等）前提
    static uint8_t *g_curr = nullptr;
    static uint8_t *g_end = nullptr;
    static uint64_t g_cap = 0;

} // anon

namespace heap
{

    bool init(uint64_t bytes)
    {
        if (bytes == 0)
            bytes = 1ull << 20; // デフォルト1MiB
        uint64_t need_pages = align_up(bytes, PAGE) / PAGE;

        // 連続ページを一括確保（非拡張）
        void *mem = pmm::alloc_pages(need_pages);
        if (!mem)
            return false;

        g_base = (uint8_t *)mem;
        g_curr = g_base;
        g_end = g_base + need_pages * PAGE;
        g_cap = need_pages * PAGE;
        return true;
    }

    void *kmalloc(size_t size, size_t align, bool zero)
    {
        if (!g_base || size == 0)
            return nullptr;
        if (align == 0)
            align = 1;

        uint64_t p = (uint64_t)g_curr;
        p = align_up(p, align);
        uint64_t q = p + size;
        if (q > (uint64_t)g_end)
            return nullptr; // 容量オーバー

        g_curr = (uint8_t *)q;

        if (zero)
        {
            // 小さな memset（簡易）
            for (uint64_t i = p; i < q; ++i)
                *(volatile uint8_t *)i = 0;
        }
        return (void *)p;
    }

    void kfree(void *)
    {
        // bump版は解放しない（将来の実装で差し替え）
    }

    void *krealloc(void *p, size_t new_size)
    {
        if (p == nullptr)
            return kmalloc(new_size);
        // bump版: 新しい領域を確保して古い内容は（必要なら）呼び出し側でコピー推奨
        return kmalloc(new_size);
    }

    uint64_t capacity() { return g_cap; }
    uint64_t used() { return (g_base && g_curr) ? (uint64_t)(g_curr - g_base) : 0; }
    uint64_t remain() { return (g_cap >= used()) ? (g_cap - used()) : 0; }

} // namespace heap
