#include "paging.hpp"
#include "pmm.hpp"

struct EFIMemoryDescriptor
{
    uint32_t Type;
    uint32_t Pad;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
};

namespace
{
    constexpr uint64_t PAGE = 4096;

    static uint8_t *bitmap = nullptr; // ビットマップ格納先(物理ではなくカーネル直アクセス可能領域)
    static uint64_t bitcap = 0;       // 管理フレーム数
    static uint64_t free_pages_cnt = 0;
    static uint64_t total_pages_cnt = 0;

    inline void bset(uint64_t i) { bitmap[i >> 3] |= (1u << (i & 7)); }
    inline void bclr(uint64_t i) { bitmap[i >> 3] &= ~(1u << (i & 7)); }
    inline bool bget(uint64_t i) { return (bitmap[i >> 3] >> (i & 7)) & 1u; }

}

// 初期化
uint64_t pmm::init(const BootInfo &bi)
{
    // 管理上限 = メモリマップの最大終端を採用
    uint64_t max_phys = 0;
    const uint8_t *base = (const uint8_t *)(uintptr_t)bi.mmap_ptr;
    const uint32_t entries = (uint32_t)(bi.mmap_size / bi.mmap_desc_size);
    for (uint32_t i = 0; i < entries; i++)
    {
        auto *d = (const EFIMemoryDescriptor *)(base + (uint64_t)i * bi.mmap_desc_size);
        uint64_t end = d->PhysicalStart + d->NumberOfPages * PAGE;
        if (end > max_phys)
            max_phys = end;
    }
    bitcap = (max_phys + PAGE - 1) / PAGE;
    uint64_t bytes = (bitcap + 7) / 8;

    // ビットマップ領域を早期確保（4KiBページを必要分）
    uint64_t need = (bytes + PAGE - 1) / PAGE;
    bitmap = (uint8_t *)paging::alloc_page4k();
    for (uint64_t i = 1; i < need; i++)
        paging::alloc_page4k(); // 連続前提でなくても可
    // 0で初期化
    for (uint64_t i = 0; i < need * PAGE; i++)
        bitmap[i] = 0;

    // まず全フレームを「使用中(1)」にしてから、空き(Conventional=7)だけ 0 に戻す
    for (uint64_t i = 0; i < bitcap; i++)
        bset(i);

    free_pages_cnt = 0;
    total_pages_cnt = 0;

    for (uint32_t i = 0; i < entries; i++)
    {
        auto *d = (const EFIMemoryDescriptor *)(base + (uint64_t)i * bi.mmap_desc_size);
        uint64_t start = d->PhysicalStart;
        uint64_t pages = d->NumberOfPages;
        total_pages_cnt += pages;
        if (d->Type == 7)
        { // Conventional
            for (uint64_t p = 0; p < pages; ++p)
            {
                uint64_t idx = (start / PAGE) + p;
                if (idx < bitcap)
                {
                    bclr(idx);
                    free_pages_cnt++;
                }
            }
        }
    }

    // 予約したい領域を明示的に使用中に戻す（フレームバッファ等）
    uint64_t fb_start = bi.fb_base / PAGE;
    uint64_t fb_pages = (bi.fb_size + PAGE - 1) / PAGE;
    for (uint64_t p = 0; p < fb_pages; ++p)
    {
        uint64_t idx = fb_start + p;
        if (idx < bitcap && !bget(idx))
        {
            bset(idx);
            free_pages_cnt--;
        }
    }

    return max_phys;
}

void *pmm::alloc_pages(uint64_t npages)
{
    if (npages == 0)
        return nullptr;
    uint64_t run = 0, start = 0;
    for (uint64_t i = 0; i < bitcap; i++)
    {
        if (!bget(i))
        {
            if (run == 0)
                start = i;
            run++;
            if (run == npages)
            {
                for (uint64_t j = 0; j < npages; j++)
                    bset(start + j);
                free_pages_cnt -= npages;
                return (void *)(uintptr_t)((start)*PAGE);
            }
        }
        else
        {
            run = 0;
        }
    }
    return nullptr; // 連続領域が無い
}

void pmm::free_pages(void *phys, uint64_t npages)
{
    if (!phys || npages == 0)
        return;
    uint64_t idx = (uint64_t)(uintptr_t)phys / PAGE;
    for (uint64_t j = 0; j < npages; j++)
    {
        if (idx + j < bitcap && bget(idx + j))
        {
            bclr(idx + j);
            free_pages_cnt++;
        }
    }
}

uint64_t pmm::total_bytes() { return total_pages_cnt * PAGE; }
uint64_t pmm::free_bytes() { return free_pages_cnt * PAGE; }
uint64_t pmm::used_bytes() { return total_bytes() - free_bytes(); }
