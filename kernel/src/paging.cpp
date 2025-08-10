#include "paging.hpp"

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
    constexpr uint64_t PAGE_4K = 0x1000;
    constexpr uint64_t PAGE_2M = 0x200000;

    constexpr uint64_t PTE_P = 1ull << 0;  // Present
    constexpr uint64_t PTE_RW = 1ull << 1; // Writable
    constexpr uint64_t PTE_US = 1ull << 2; // User/Supervisor (0=supervisor)
    constexpr uint64_t PTE_PS = 1ull << 7; // Page Size (on PD: 2MiB)
    constexpr uint64_t PTE_G = 1ull << 8;  // Global (任意)

    // 超簡易バンプアロケータ（4KiB単位）: memmapの最初のConventionalから切り出す
    static uint8_t *bump_base = nullptr;
    static uint8_t *bump_ptr = nullptr;
    static uint8_t *bump_end = nullptr;

    bool bump_inited = false;

    inline uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

    static bool init_bump_from_memmap(const BootInfo &bi)
    {
        if (!bi.mmap_ptr || !bi.mmap_size || !bi.mmap_desc_size)
            return false;
        const uint8_t *base = (const uint8_t *)(uintptr_t)bi.mmap_ptr;
        const uint32_t entries = (uint32_t)(bi.mmap_size / bi.mmap_desc_size);
        for (uint32_t i = 0; i < entries; i++)
        {
            auto *d = (const EFIMemoryDescriptor *)(base + (uint64_t)i * bi.mmap_desc_size);
            if (d->Type == 7 /*Conventional*/ && d->NumberOfPages >= 256)
            {
                uint64_t phys = align_up(d->PhysicalStart, PAGE_4K);
                uint64_t bytes = d->NumberOfPages * PAGE_4K;
                bump_base = (uint8_t *)(uintptr_t)phys;
                bump_ptr = (uint8_t *)(uintptr_t)phys;
                bump_end = (uint8_t *)(uintptr_t)(phys + bytes);
                return true;
            }
        }
        return false;
    }

    // マッピング終端（デバッグ表示用）
    uint64_t g_mapped_limit = 0;

} // anon

namespace paging
{

    void *alloc_page4k()
    {
        if (!bump_ptr)
            return nullptr;
        if ((uint64_t)(bump_end - bump_ptr) < PAGE_4K)
            return nullptr;
        void *p = bump_ptr;
        bump_ptr += PAGE_4K;
        // クリア（必須ではないが安心）
        uint64_t *q = (uint64_t *)p;
        for (int i = 0; i < (PAGE_4K / 8); ++i)
            q[i] = 0;
        return p;
    }

    uint64_t mapped_limit() { return g_mapped_limit; }

    bool init_allocator(const BootInfo &bi)
    {
        if (bump_inited && bump_ptr)
            return true; // ★二重初期化防止
        if (!init_bump_from_memmap(bi))
            return false;
        bump_inited = true;
        return true;
    }

    // paging.cpp （alloc_page4k()の下あたり）
    void *alloc_low_stack(size_t bytes)
    {
        if (bytes == 0)
            bytes = 4096;
        // 4KiB単位に切り上げ
        uint64_t need = (bytes + PAGE_4K - 1) & ~(PAGE_4K - 1);
        uint8_t *base = (uint8_t *)alloc_page4k();
        if (!base)
            return nullptr;
        // 1ページ目は既に確保済み。残りを確保してクリア
        uint64_t remain = need - PAGE_4K;
        while (remain)
        {
            if (!alloc_page4k())
                return nullptr;
            remain -= PAGE_4K;
        }
        return (void *)(base + need); // 上成長のスタックを想定し、トップを返す
    }

    uint64_t init_identity(const BootInfo &bi)
    {
        if (!init_allocator(bi))
            return 0;

        // PML4 / PDPT / PD を確保
        uint64_t *pml4 = (uint64_t *)alloc_page4k();
        uint64_t *pdpt = (uint64_t *)alloc_page4k();
        if (!pml4 || !pdpt)
            return 0;

        // PML4[0] -> PDPT
        pml4[0] = ((uint64_t)(uintptr_t)pdpt) | PTE_P | PTE_RW;

        // どこまで恒等マップするか: RAM終端とフレームバッファ終端の max を採用
        // RAM は BootServicesData/Code & Conventional を対象に（前ブランチの方針踏襲）
        uint64_t max_phys = 0;
        if (bi.mmap_ptr && bi.mmap_size && bi.mmap_desc_size)
        {
            const uint8_t *base = (const uint8_t *)(uintptr_t)bi.mmap_ptr;
            const uint32_t entries = (uint32_t)(bi.mmap_size / bi.mmap_desc_size);
            for (uint32_t i = 0; i < entries; ++i)
            {
                auto *d = (const EFIMemoryDescriptor *)(base + (uint64_t)i * bi.mmap_desc_size);
                // ★型で絞らず、「その領域の物理終端」で常に最大を更新
                uint64_t end = d->PhysicalStart + d->NumberOfPages * PAGE_4K;
                if (end > max_phys)
                    max_phys = end;
            }
        }

        // GOP の終端も反映
        uint64_t fb_end = bi.fb_base + bi.fb_size;
        if (fb_end > max_phys)
            max_phys = fb_end;

        // ★現在の RSP を反映
        uint64_t cur_rsp;
        asm volatile("mov %%rsp, %0" : "=r"(cur_rsp));
        if (cur_rsp > max_phys)
            max_phys = cur_rsp + (16ull << 20); // 16MiB余裕

        // ★現在の RIP も反映（“今実行中のコード”が未マップだと即落ちる）
        uint64_t cur_rip;
        asm volatile("lea (%%rip), %0" : "=r"(cur_rip));
        if (cur_rip > max_phys)
            max_phys = cur_rip + (16ull << 20);

        // 最低でも 64MiB
        if (max_phys < (64ull << 20))
            max_phys = (64ull << 20);

        // 2MiBアライン
        max_phys = align_up(max_phys, PAGE_2M);

        auto align_up = [](uint64_t v, uint64_t a)
        { return (v + a - 1) & ~(a - 1); };
        max_phys = align_up(max_phys, PAGE_2M);
        uint64_t gib = (1ull << 30);
        uint32_t num_gibs = (uint32_t)((max_phys + gib - 1) / gib);

        // PD を必要数だけ確保し、PDPT[gi] に繋ぐ
        for (uint32_t gi = 0; gi < num_gibs; ++gi)
        {
            uint64_t *pd = (uint64_t *)alloc_page4k();
            if (!pd)
                return 0;
            pdpt[gi] = ((uint64_t)(uintptr_t)pd) | PTE_P | PTE_RW;

            // このGiBの先頭物理アドレス
            uint64_t base = (uint64_t)gi * gib;

            // 2MiBページを 512 個（=1GiB）敷く
            for (uint32_t ei = 0; ei < 512; ++ei)
            {
                uint64_t addr = base + (uint64_t)ei * PAGE_2M;
                if (addr >= max_phys)
                    break;
                pd[ei] = addr | PTE_P | PTE_RW | PTE_PS | PTE_G;
            }
        }

        // デバッグ用：実際にマップした終端
        g_mapped_limit = (uint64_t)num_gibs * gib;

        asm volatile("cli");

        // CR3 切替
        uint64_t new_cr3 = (uint64_t)(uintptr_t)pml4;
        asm volatile("mov %0, %%cr3" ::"r"(new_cr3) : "memory");
        return new_cr3;
    }

} // namespace paging
