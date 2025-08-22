#include "../../uefi/include/efi/base.h"
#include "console.hpp"
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
    constexpr uint64_t PAGE_4K = 0x1000;
    constexpr uint64_t PAGE_2M = 0x200000;

    constexpr uint64_t MMIO_BOUNDARY = 64ULL * 1024 * 1024 * 1024 * 1024;

    constexpr uint64_t PTE_P = 1ull << 0;  // Present
    constexpr uint64_t PTE_RW = 1ull << 1; // Writable
    constexpr uint64_t PTE_US = 1ull << 2; // User/Supervisor (0=supervisor)
    constexpr uint64_t PTE_PS = 1ull << 7; // Page Size (on PD: 2MiB)
    constexpr uint64_t PTE_G = 1ull << 8;  // Global (任意)

    constexpr uint64_t PML4_SHIFT = 39;
    constexpr uint64_t PDPT_SHIFT = 30;
    constexpr uint64_t PD_SHIFT = 21;

    constexpr uint64_t IDX_MASK = 0x1FF; // 9bit

    // ページテーブルエントリのビット
    constexpr uint64_t P_PRESENT = 1ull << 0;
    constexpr uint64_t P_RW = 1ull << 1;
    constexpr uint64_t P_US = 1ull << 2;
    constexpr uint64_t P_PWT = 1ull << 3; // write-through
    constexpr uint64_t P_PCD = 1ull << 4; // cache-disable
    constexpr uint64_t P_ACCESSED = 1ull << 5;
    constexpr uint64_t P_DIRTY = 1ull << 6;
    constexpr uint64_t P_PS = 1ull << 7; // 1=2MiB/1GiB page
    constexpr uint64_t P_GLOBAL = 1ull << 8;
    constexpr uint64_t P_NX = 1ull << 63;

    // 超簡易バンプアロケータ（4KiB単位）: memmapの最初のConventionalから切り出す
    static uint8_t *bump_base = nullptr;
    static uint8_t *bump_ptr = nullptr;
    static uint8_t *bump_end = nullptr;

    bool bump_inited = false;

    alignas(PAGE_4K) uint8_t g_pre_paging_allocator_pool[PAGE_4K * 64]; // 256KiB
    uint8_t *g_pool_ptr = nullptr;

    inline uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

    inline uint64_t *phys_to_virt(uint64_t phys)
    {
        // 恒等マップ前提なら物理==仮想。HHDM等がある場合はそこに合わせる。
        return reinterpret_cast<uint64_t *>(phys);
    }

    static uint64_t alloc_zero_page_phys()
    {
        void *p = pmm::alloc_pages(1);
        if (!p)
            return 0;
        // ゼロ化
        uint64_t *q = (uint64_t *)p;
        for (int i = 0; i < (PAGE_4K / 8); ++i)
            q[i] = 0;
        return (uint64_t)(uintptr_t)p;
    }

    // paging.cpp のグローバルスコープ
    // paging.cpp
    static uint64_t *ensure_child(uint64_t *parent, uint64_t idx)
    {
        uint64_t entry = parent[idx];
        if (entry & P_PRESENT)
        {
            return phys_to_virt(entry & ~0xFFFULL);
        }

        void *child_page = paging::alloc_page4k();
        if (!child_page)
        {
            return nullptr;
        }

        uint64_t child_phys = (uint64_t)(uintptr_t)child_page;
        const uint64_t flags = P_PRESENT | P_RW | P_US | P_PWT | P_PCD;
        parent[idx] = child_phys | flags;

        return reinterpret_cast<uint64_t *>(child_page);
    }

    static uint64_t *ensure_child_with_pmm(uint64_t *parent_virt_ptr, uint64_t idx)
    {
        // 親テーブルのエントリを確認
        uint64_t entry = parent_virt_ptr[idx];
        if (entry & P_PRESENT)
        {
            // 既に存在する場合、エントリ内の物理アドレスを仮想アドレスに変換して返す
            return phys_to_virt(entry & ~0xFFFULL);
        }

        // 新しいページを物理メモリから確保
        void *child_page_phys_ptr = pmm::alloc_pages(1);
        if (!child_page_phys_ptr)
        {
            return nullptr;
        }

        // ★★★ここが重要★★★
        // 確保した物理ページにアクセスするための「仮想アドレス」ポインタを取得
        uint64_t *child_page_virt_ptr = phys_to_virt((uint64_t)child_page_phys_ptr);

        // 仮想アドレスを使ってページをゼロクリア
        for (int i = 0; i < 512; ++i)
        {
            child_page_virt_ptr[i] = 0;
        }

        // 親テーブルには「物理アドレス」を書き込む
        const uint64_t flags = P_PRESENT | P_RW | P_US;
        parent_virt_ptr[idx] = (uint64_t)child_page_phys_ptr | flags;

        // 呼び出し元（カーネル）には「仮想アドレス」を返す
        return child_page_virt_ptr;
    }

    static bool init_bump_from_memmap(const BootInfo &bi)
    {
        if (!bi.mmap_ptr || !bi.mmap_size || !bi.mmap_desc_size)
            return false;

        // 1. カーネルが使用している物理メモリ範囲のリストを作成
        constexpr int kMaxKernelRanges = 16;
        PhysRange kernel_ranges[kMaxKernelRanges];
        int kr_count = 0;
        if (bi.kernel_ranges_ptr && bi.kernel_ranges_cnt > 0 && bi.kernel_ranges_cnt < kMaxKernelRanges)
        {
            auto *kr = (const PhysRange *)(uintptr_t)bi.kernel_ranges_ptr;
            for (uint32_t i = 0; i < bi.kernel_ranges_cnt; ++i)
            {
                kernel_ranges[kr_count++] = kr[i];
            }
        }

        const uint8_t *base = (const uint8_t *)(uintptr_t)bi.mmap_ptr;
        const uint32_t entries = (uint32_t)(bi.mmap_size / bi.mmap_desc_size);

        // 2. メモリマップを走査し、カーネルと重ならない十分な大きさの領域を探す
        for (uint32_t i = 0; i < entries; i++)
        {
            auto *d = (const EFIMemoryDescriptor *)(base + (uint64_t)i * bi.mmap_desc_size);

            if (d->Type != EfiConventionalMemory || d->NumberOfPages < 256)
            {
                continue; // 通常メモリで1MiB以上の領域のみを対象
            }

            uint64_t region_start = d->PhysicalStart;
            uint64_t region_end = region_start + d->NumberOfPages * 4096;

            // カーネル領域との重複チェック
            bool overlaps = false;
            for (int k = 0; k < kr_count; ++k)
            {
                uint64_t kr_start = kernel_ranges[k].base;
                uint64_t kr_end = kr_start + kernel_ranges[k].pages * 4096;
                // 重複条件: max(start1, start2) < min(end1, end2)
                if ((region_start > kr_start ? region_start : kr_start) < (region_end < kr_end ? region_end : kr_end))
                {
                    overlaps = true;
                    break;
                }
            }

            if (!overlaps)
            {
                // 重複がなく、条件を満たす最初の領域をアロケータとして使用
                uint64_t phys = align_up(region_start, PAGE_4K);
                uint64_t bytes = (region_end - phys);
                bump_base = (uint8_t *)(uintptr_t)phys;
                bump_ptr = (uint8_t *)(uintptr_t)phys;
                bump_end = (uint8_t *)(uintptr_t)(phys + bytes);
                return true;
            }
        }

        // 適切な領域が見つからなかった
        return false;
    }

    // マッピング終端（デバッグ表示用）
    uint64_t g_mapped_limit = 0;

    extern "C" uint64_t paging_get_cr3_phys(); // 例えば paging::get_cr3() が返す物理

    // テーブルを「存在しなければ作る」。
    // 戻り値は子テーブル（次段）の仮想アドレス。
    uint64_t *touch_table(uint64_t *parent, uint64_t idx)
    {
        uint64_t e = parent[idx];
        if (e & P_PRESENT)
        {
            uint64_t child_phys = e & ~0xFFFULL;
            return phys_to_virt(child_phys);
        }
        // 4KiBページ1枚確保してゼロクリア
        void *pg = pmm::alloc_pages(1);
        if (!pg)
            return nullptr;
        // ゼロクリア
        for (size_t i = 0; i < PAGE_4K; i++)
            reinterpret_cast<volatile uint8_t *>(pg)[i] = 0;

        uint64_t child_phys = reinterpret_cast<uint64_t>(pg);
        parent[idx] = (child_phys) | P_PRESENT | P_RW | P_PCD | P_PWT; // テーブル自体もUC
        return reinterpret_cast<uint64_t *>(pg);
    }

    inline void invlpg(uint64_t va)
    {
        asm volatile("invlpg (%0)" ::"r"(va) : "memory");
    }

    static bool probe_va(uint64_t va)
    {
        uint64_t *pml4 = phys_to_virt(paging_get_cr3_phys());
        uint64_t l4i = (va >> PML4_SHIFT) & IDX_MASK;
        uint64_t l3i = (va >> PDPT_SHIFT) & IDX_MASK;
        uint64_t l2i = (va >> PD_SHIFT) & IDX_MASK;

        uint64_t e4 = pml4[l4i];
        if (!(e4 & P_PRESENT))
            return false;
        uint64_t *pdpt = phys_to_virt(e4 & ~0xFFFULL);

        uint64_t e3 = pdpt[l3i];
        if (!(e3 & P_PRESENT))
            return false;
        uint64_t *pd = phys_to_virt(e3 & ~0xFFFULL);

        uint64_t e2 = pd[l2i];
        return (e2 & P_PRESENT) != 0;
    }

    inline void reload_cr3()
    {
        uint64_t cr3;
        asm volatile("mov %%cr3,%0" : "=r"(cr3));
        asm volatile("mov %0,%%cr3" ::"r"(cr3) : "memory");
    }

} // anon

extern "C" uint64_t paging_get_cr3_phys()
{
    uint64_t phys;
    asm volatile("mov %%cr3, %0" : "=r"(phys));
    return phys & ~0xFFFULL; // 下位12bitはフラグなのでクリア
}

namespace paging
{

    void *alloc_page4k()
    {
        if (!g_pool_ptr)
            return nullptr;

        uint8_t *current_ptr = g_pool_ptr;
        uint8_t *pool_end = g_pre_paging_allocator_pool + sizeof(g_pre_paging_allocator_pool);

        if (current_ptr + PAGE_4K > pool_end)
        {
            // プールが枯渇した
            return nullptr;
        }

        g_pool_ptr += PAGE_4K;

        // ページをゼロクリア
        volatile uint64_t *q = (volatile uint64_t *)current_ptr;
        for (int i = 0; i < (PAGE_4K / 8); ++i)
            q[i] = 0;

        return current_ptr;
    }

    uint64_t mapped_limit() { return g_mapped_limit; }

    bool init_allocator(const BootInfo &bi)
    {
        g_pool_ptr = g_pre_paging_allocator_pool;
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
        /*
        Framebuffer fb(bi);
        fb.clear({10, 12, 24});
        Painter paint(fb);
        Console con(fb, paint); // printfデバッグ用にConsoleを一時作成
        con.println("paging::init_identity: Entered function.");
        */

        if (!init_allocator(bi))
        {
            // con.println("paging::init_identity: ERROR - init_allocator failed.");
            return 0;
        }
        // con.println("paging::init_identity: Static allocator initialized.");

        uint64_t *pml4 = (uint64_t *)alloc_page4k();
        if (!pml4)
        {
            // con.println("paging::init_identity: ERROR - PML4 allocation failed.");
            return 0;
        }
        // con.printf("paging::init_identity: PML4 allocated within static pool.\n");

        uint64_t max_phys_addr = 0;
        if (bi.mmap_ptr && bi.mmap_size > 0 && bi.mmap_desc_size > 0)
        {
            auto *desc_ptr = (const uint8_t *)bi.mmap_ptr;
            const uint32_t entries = bi.mmap_size / bi.mmap_desc_size;
            for (uint32_t i = 0; i < entries; ++i)
            {
                auto *desc = (const EFIMemoryDescriptor *)desc_ptr;
                //con.printf("type=%u, addr=%p\n", desc->Type, desc->PhysicalStart);

                if (desc->Type == EfiConventionalMemory || desc->Type == EfiBootServicesCode || desc->Type == EfiBootServicesData)
                {
                    if (desc->PhysicalStart >= MMIO_BOUNDARY)
                    {
                        desc_ptr += bi.mmap_desc_size;
                        continue;
                    }
                    uint64_t end_addr = desc->PhysicalStart + (desc->NumberOfPages * 4096);
                    if (end_addr > max_phys_addr)
                    {
                        max_phys_addr = end_addr;
                    }
                }
                desc_ptr += bi.mmap_desc_size;
            }
        }
        uint64_t fb_end = bi.fb_base + bi.fb_size;
        if (fb_end > max_phys_addr)
            max_phys_addr = fb_end;
        max_phys_addr = align_up(max_phys_addr, PAGE_2M);
        if (max_phys_addr == 0)
            max_phys_addr = 64 * 1024 * 1024;
        //con.printf("paging::init_identity: Max physical address to map: 0x%p\n", (void *)max_phys_addr);

        // con.println("paging::init_identity: Starting page table construction loop...");
        const uint64_t pte_flags = P_PRESENT | P_RW | P_PS | P_GLOBAL;
        for (uint64_t paddr = 0; paddr < max_phys_addr; paddr += PAGE_2M)
        {
            const uint64_t va = paddr;
            uint64_t *pdpt = ensure_child(pml4, (va >> PML4_SHIFT) & IDX_MASK);
            if (!pdpt)
            {
                // con.println("\nERROR: ensure_child for PDPT failed.");
                return 0;
            }

            uint64_t *pd = ensure_child(pdpt, (va >> PDPT_SHIFT) & IDX_MASK);
            if (!pd)
            {
                // con.println("\nERROR: ensure_child for PD failed.");
                return 0;
            }

            pd[(va >> PD_SHIFT) & IDX_MASK] = paddr | pte_flags;
        }
        // con.println("paging::init_identity: Page table construction loop finished.");

        g_mapped_limit = max_phys_addr;
        asm volatile("cli");
        // con.println("paging::init_identity: About to load new CR3...");
        uint64_t new_cr3 = (uint64_t)(uintptr_t)pml4;
        asm volatile("mov %0, %%cr3" ::"r"(new_cr3) : "memory");
        // con.println("paging::init_identity: New CR3 loaded. Function will now return.");

        return new_cr3;
    }

    bool map_mmio_at(uint64_t va, uint64_t phys, uint64_t size)
    {
        if (size == 0)
            return true;

        // 2MiB 単位に正規化
        uint64_t va0 = va & ~(PAGE_2M - 1);
        uint64_t pa0 = phys & ~(PAGE_2M - 1);
        uint64_t pages = (((va & (PAGE_2M - 1)) + size + PAGE_2M - 1) / PAGE_2M);

        uint64_t *pml4 = phys_to_virt(paging_get_cr3_phys());
        for (uint64_t i = 0; i < pages; ++i)
        {
            uint64_t cur_va = va0 + i * PAGE_2M;
            uint64_t cur_pa = pa0 + i * PAGE_2M;

            uint64_t l4i = (cur_va >> PML4_SHIFT) & IDX_MASK;
            uint64_t l3i = (cur_va >> PDPT_SHIFT) & IDX_MASK;
            uint64_t l2i = (cur_va >> PD_SHIFT) & IDX_MASK;

            uint64_t *pdpt = ensure_child_with_pmm(pml4, l4i);
            if (!pdpt)
                return false;
            uint64_t *pd = ensure_child_with_pmm(pdpt, l3i);
            if (!pd)
                return false;
            // ▲▲▲▲▲▲

            // UC(非キャッシュ) + NX の 2MiB 大ページ
            uint64_t flags = P_PRESENT | P_RW | P_PWT | P_PCD | P_PS | P_NX;
            pd[l2i] = (cur_pa & ~0x1FFFFFULL) | flags;
        }
        reload_cr3(); // TLB整理
        return true;
    }

    bool map_mmio_range(uint64_t phys, uint64_t size)
    {
        if (size == 0)
            return true;

        // 2MiBアラインで全面カバー
        uint64_t start = phys & ~(PAGE_2M - 1);
        uint64_t end = (phys + size + PAGE_2M - 1) & ~(PAGE_2M - 1);

        uint64_t *pml4 = phys_to_virt(paging_get_cr3_phys());

        for (uint64_t addr = start; addr < end; addr += PAGE_2M)
        {
            uint64_t l4i = (addr >> PML4_SHIFT) & IDX_MASK;
            uint64_t l3i = (addr >> PDPT_SHIFT) & IDX_MASK;
            uint64_t l2i = (addr >> PD_SHIFT) & IDX_MASK;

            uint64_t *pdpt = ensure_child(pml4, l4i);
            if (!pdpt)
                return false;

            uint64_t *pd = ensure_child(pdpt, l3i);
            if (!pd)
                return false;

            // 既に何か貼ってあれば上書きでもOK（UCを保証したい）
            uint64_t flags = P_PRESENT | P_RW | P_PWT | P_PCD | P_PS | P_NX;
            pd[l2i] = (addr & ~0x1FFFFFULL) | flags;
        }

        // TLBの不定を避けるためにCR3再読み込み（invlpgループでも可）
        reload_cr3();
        return true;
    }

    uint64_t paging::virt_to_phys(uint64_t va)
    {
        // ルートPML4（物理→恒等マップの仮定でそのままKVAへ）
        uint64_t *pml4 = phys_to_virt(paging_get_cr3_phys());

        // 各段のインデックス
        const uint64_t l4i = (va >> PML4_SHIFT) & IDX_MASK;
        const uint64_t l3i = (va >> PDPT_SHIFT) & IDX_MASK;
        const uint64_t l2i = (va >> PD_SHIFT) & IDX_MASK;
        const uint64_t l1i = (va >> 12) & IDX_MASK;

        // L4
        uint64_t e4 = pml4[l4i];
        if (!(e4 & P_PRESENT))
            return -1;
        uint64_t *pdpt = phys_to_virt(e4 & ~0xFFFULL);

        // L3
        uint64_t e3 = pdpt[l3i];
        if (!(e3 & P_PRESENT))
            return -1;
        // 1GiB ページは使っていない想定なので PS=1 は未対応にしておく（必要なら足せる）
        if (e3 & P_PS)
        {
            // 1GiB 大ページ（未使用想定）
            uint64_t phys_base = e3 & ~((1ULL << 30) - 1);
            return phys_base | (va & ((1ULL << 30) - 1));
        }
        uint64_t *pd = phys_to_virt(e3 & ~0xFFFULL);

        // L2
        uint64_t e2 = pd[l2i];
        if (!(e2 & P_PRESENT))
            return -1;

        if (e2 & P_PS)
        {
            // 2MiB 大ページ
            uint64_t phys_base = e2 & ~0x1FFFFFULL; // 下位21bitはオフセット
            return phys_base | (va & 0x1FFFFFULL);
        }

        // L1（4KiB ページ）
        uint64_t *pt = phys_to_virt(e2 & ~0xFFFULL);
        uint64_t e1 = pt[l1i];
        if (!(e1 & P_PRESENT))
            return -1;

        uint64_t phys_base = e1 & ~0xFFFULL;
        return phys_base | (va & 0xFFFULL);
    }

    bool dbg_probe_mmio_mapped(uint64_t phys_addr)
    {
        return probe_va(phys_addr);
    }

} // namespace paging
