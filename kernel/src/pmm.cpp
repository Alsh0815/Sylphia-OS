#include "paging.hpp"
#include "pmm.hpp"

struct EFIMemoryDescriptor
{
    uint32_t Type;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
};

namespace
{
    constexpr uint64_t MMIO_BOUNDARY = 64ULL * 1024 * 1024 * 1024 * 1024;
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
    // 1. 管理する物理メモリの最大アドレスを決定
    uint64_t max_phys = 0;
    const uint8_t *base = (const uint8_t *)(uintptr_t)bi.mmap_ptr;
    const uint32_t entries = (uint32_t)(bi.mmap_size / bi.mmap_desc_size);
    for (uint32_t i = 0; i < entries; i++)
    {
        auto *d = (const EFIMemoryDescriptor *)(base + (uint64_t)i * bi.mmap_desc_size);

        // ▼▼▼ paging.cpp と同じロジックをここにも適用 ▼▼▼
        if (d->PhysicalStart >= MMIO_BOUNDARY)
        {
            continue;
        }

        uint64_t end = d->PhysicalStart + d->NumberOfPages * PAGE;

        if (end > max_phys)
        {
            max_phys = end;
        }
        // ▲▲▲▲▲▲
    }
    bitcap = (max_phys + PAGE - 1) / PAGE;
    uint64_t bytes_for_bitmap = (bitcap + 7) / 8;
    uint64_t pages_for_bitmap = (bytes_for_bitmap + PAGE - 1) / PAGE;

    // 2. ビットマップ自体を格納するための空き領域をメモリマップから探す
    bool bitmap_allocated = false;
    for (uint32_t i = 0; i < entries; i++)
    {
        auto *d = (const EFIMemoryDescriptor *)(base + (uint64_t)i * bi.mmap_desc_size);
        // カーネル自身と重ならず、かつビットマップを格納できる大きさの通常メモリ領域を探す
        if (d->Type == EfiConventionalMemory && d->NumberOfPages >= pages_for_bitmap)
        {
            // ここにビットマップを配置する
            bitmap = (uint8_t *)(uintptr_t)d->PhysicalStart;
            bitmap_allocated = true;
            break;
        }
    }

    if (!bitmap_allocated)
    {
        // 致命的エラー：ビットマップを置く場所がない
        return 0;
    }

    // 3. ビットマップを初期化
    // まず全フレームを「使用中(1)」に設定
    for (uint64_t i = 0; i < (pages_for_bitmap * PAGE); i++)
        bitmap[i] = 0xFF;

    // 4. メモリマップに基づき、空き領域を「空き(0)」に設定
    free_pages_cnt = 0;
    total_pages_cnt = 0;
    for (uint32_t i = 0; i < entries; i++)
    {
        auto *d = (const EFIMemoryDescriptor *)(base + (uint64_t)i * bi.mmap_desc_size);
        total_pages_cnt += d->NumberOfPages;
        if (d->Type == EfiConventionalMemory)
        {
            uint64_t start_idx = d->PhysicalStart / PAGE;
            for (uint64_t p = 0; p < d->NumberOfPages; ++p)
            {
                uint64_t idx = start_idx + p;
                if (idx < bitcap)
                {
                    bclr(idx);
                    free_pages_cnt++;
                }
            }
        }
    }

    // 5. 最後に、ビットマップ自身が使用している領域を「使用中」として予約
    reserve_range((uint64_t)(uintptr_t)bitmap, pages_for_bitmap);

    // フレームバッファ領域も予約
    uint64_t fb_start = bi.fb_base;
    uint64_t fb_pages = (bi.fb_size + PAGE - 1) / PAGE;
    reserve_range(fb_start, fb_pages);

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

void pmm::reserve_range(uint64_t phys_base, uint64_t pages)
{
    if (!bitmap || pages == 0)
        return;
    uint64_t idx = phys_base / PAGE;
    for (uint64_t j = 0; j < pages; ++j)
    {
        if (idx + j < bitcap && !bget(idx + j))
        {
            bset(idx + j);
            if (free_pages_cnt)
                free_pages_cnt--;
        }
    }
}