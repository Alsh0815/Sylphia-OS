#include <stddef.h>
#include "paging.hpp"
#include "memory/memory_manager.hpp"
#include "cxx.hpp"    // memset用
#include "printk.hpp" // デバッグ用

PML4Table *PageManager::pml4_table_ = nullptr;

PageTable *PageManager::AllocateTable()
{
    // メモリマネージャから1フレーム(4KB)もらう
    void *ptr = MemoryManager::AllocateFrame();
    if (!ptr)
        return nullptr;

    // 必ず0クリアする
    memset(ptr, 0, 4096);
    return static_cast<PageTable *>(ptr);
}

void PageManager::MapPage(uint64_t virtual_addr, uint64_t physical_addr, size_t count)
{
    if (!pml4_table_)
        return;

    for (size_t i = 0; i < count; ++i)
    {
        uint64_t vaddr = virtual_addr + (i * kPageSize4K);
        uint64_t paddr = physical_addr + (i * kPageSize4K);

        uint64_t pml4_idx = (vaddr >> 39) & 0x1FF;
        uint64_t pdp_idx = (vaddr >> 30) & 0x1FF;
        uint64_t pd_idx = (vaddr >> 21) & 0x1FF;
        uint64_t pt_idx = (vaddr >> 12) & 0x1FF;

        // --- Level 4 (PML4) ---
        if (!pml4_table_->entries[pml4_idx].bits.present)
        {
            PageTable *new_table = AllocateTable();
            if (!new_table)
                return; // メモリ不足時は中断
            pml4_table_->entries[pml4_idx].SetAddress(reinterpret_cast<uint64_t>(new_table));
            pml4_table_->entries[pml4_idx].bits.present = 1;
            pml4_table_->entries[pml4_idx].bits.read_write = 1;
        }
        PageTable *pdp_table = reinterpret_cast<PageTable *>(pml4_table_->entries[pml4_idx].GetAddress());

        // --- Level 3 (PDP) ---
        if (!pdp_table->entries[pdp_idx].bits.present)
        {
            PageTable *new_table = AllocateTable();
            if (!new_table)
                return;
            pdp_table->entries[pdp_idx].SetAddress(reinterpret_cast<uint64_t>(new_table));
            pdp_table->entries[pdp_idx].bits.present = 1;
            pdp_table->entries[pdp_idx].bits.read_write = 1;
        }
        PageTable *pd_table = reinterpret_cast<PageTable *>(pdp_table->entries[pdp_idx].GetAddress());

        // --- Level 2 (PD) ---
        if (!pd_table->entries[pd_idx].bits.present)
        {
            PageTable *new_table = AllocateTable();
            if (!new_table)
                return;
            pd_table->entries[pd_idx].SetAddress(reinterpret_cast<uint64_t>(new_table));
            pd_table->entries[pd_idx].bits.present = 1;
            pd_table->entries[pd_idx].bits.read_write = 1;
        }
        PageTable *pt_table = reinterpret_cast<PageTable *>(pd_table->entries[pd_idx].GetAddress());

        // --- Level 1 (PT) ---
        pt_table->entries[pt_idx].SetAddress(paddr);
        pt_table->entries[pt_idx].bits.present = 1;
        pt_table->entries[pt_idx].bits.read_write = 1;

        InvalidateTLB(vaddr);
    }
}

void PageManager::Initialize()
{
    kprintf("[Paging] Initializing with 2MB Huge Pages...\n");

    // 1. ルートテーブル (PML4) 作成
    pml4_table_ = reinterpret_cast<PML4Table *>(AllocateTable());
    if (!pml4_table_)
        while (1)
            __asm__ volatile("hlt");

    // 2. アイデンティティマッピング (0-64GB)
    // ビデオメモリ(VRAM)やMMIOデバイスが 3GB-4GB 付近やそれ以上にあるため、
    // 広範囲をカバーしておく必要がある。

    // PDPテーブル (512GB分をカバー) を1つ作成
    // 仮想アドレス 0x000... は PML4[0] に対応
    PageTable *pdp_table = AllocateTable();
    pml4_table_->entries[0].SetAddress(reinterpret_cast<uint64_t>(pdp_table));
    pml4_table_->entries[0].bits.present = 1;
    pml4_table_->entries[0].bits.read_write = 1;
    // pml4_table_->entries[0].bits.user_supervisor = 1; // ユーザーモードからもアクセス可

    // 64個のPDテーブルを作成して、64GB分 (64 * 1GB) をマップする
    for (int i_pdp = 0; i_pdp < 64; ++i_pdp)
    {
        PageTable *pd_table = AllocateTable();
        pdp_table->entries[i_pdp].SetAddress(reinterpret_cast<uint64_t>(pd_table));
        pdp_table->entries[i_pdp].bits.present = 1;
        pdp_table->entries[i_pdp].bits.read_write = 1;
        // pdp_table->entries[i_pdp].bits.user_supervisor = 1; // ユーザーモードからもアクセス可

        // PDエントリを埋める (各エントリ 2MB * 512 = 1GB)
        for (int i_pd = 0; i_pd < 512; ++i_pd)
        {
            // 物理アドレス計算: (PDP番号 * 1GB) + (PD番号 * 2MB)
            // ※ オーバーフロー防止のため 1ULL (unsigned long long) を掛ける
            uint64_t physical_addr =
                (static_cast<uint64_t>(i_pdp) * 1024 * 1024 * 1024) +
                (static_cast<uint64_t>(i_pd) * 2 * 1024 * 1024);

            pd_table->entries[i_pd].SetAddress(physical_addr);
            pd_table->entries[i_pd].bits.present = 1;
            pd_table->entries[i_pd].bits.read_write = 1;
            pd_table->entries[i_pd].bits.huge_page = 1; // 2MBページ
            // pd_table->entries[i_pd].bits.user_supervisor = 1; // ユーザーモードからもアクセス可
        }
    }

    kprintf("[Paging] Identity Mapping (0-64GB) Created.\n");

    // 3. CR3ロード (ページング有効化)
    LoadCR3(reinterpret_cast<uint64_t>(pml4_table_));

    kprintf("[Paging] CR3 Loaded. Paging is active!\n");
}