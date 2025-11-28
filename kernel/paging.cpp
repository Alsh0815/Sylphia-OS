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

bool PageManager::AllocateVirtual(uint64_t virtual_addr, size_t size, uint64_t flags)
{
    // 4KBアライメントチェック
    if (virtual_addr % kPageSize4K != 0)
        return false;

    size_t num_pages = (size + kPageSize4K - 1) / kPageSize4K;

    for (size_t i = 0; i < num_pages; ++i)
    {
        uint64_t vaddr = virtual_addr + (i * kPageSize4K);

        // 1. 物理フレームを確保
        void *frame = MemoryManager::AllocateFrame();
        if (frame == nullptr)
        {
            // メモリ不足 (本来はここでロールバックが必要)
            return false;
        }

        // 2. 物理フレームの中身をクリア (セキュリティ対策)
        // ここでは物理アドレス(identity map)経由で書き込む
        memset(frame, 0, kPageSize4K);

        // 3. マッピング
        uint64_t paddr = reinterpret_cast<uint64_t>(frame);
        MapPage(vaddr, paddr, 1, flags);
    }

    return true;
}

void PageManager::MapPage(uint64_t virtual_addr, uint64_t physical_addr, size_t count, uint64_t flags)
{
    if (!pml4_table_)
        return;

    for (size_t i = 0; i < count; ++i)
    {
        uint64_t vaddr = virtual_addr + (i * kPageSize4K);
        uint64_t paddr = physical_addr + (i * kPageSize4K);

        // ... (インデックス計算省略) ...
        uint64_t pml4_idx = (vaddr >> 39) & 0x1FF;
        uint64_t pdp_idx = (vaddr >> 30) & 0x1FF;
        uint64_t pd_idx = (vaddr >> 21) & 0x1FF;
        uint64_t pt_idx = (vaddr >> 12) & 0x1FF;

        // ... (EnsureEntry ラムダ省略) ...
        auto EnsureEntry = [&](PageTableEntry &entry) -> PageTable *
        {
            if (!entry.bits.present)
            {
                PageTable *new_table = AllocateTable();
                if (!new_table)
                    return nullptr;
                entry.SetAddress(reinterpret_cast<uint64_t>(new_table));
                entry.bits.present = 1;
                entry.bits.read_write = 1;
                entry.bits.user_supervisor = (flags & kUser) ? 1 : 0;
            }
            else
            {
                if (flags & kUser)
                {
                    entry.bits.user_supervisor = 1;
                }
            }
            return reinterpret_cast<PageTable *>(entry.GetAddress());
        };

        // --- Level 4 (PML4) ---
        PageTable *pdp_table = EnsureEntry(pml4_table_->entries[pml4_idx]);
        if (!pdp_table)
            return;

        // --- Level 3 (PDP) ---
        PageTable *pd_table = EnsureEntry(pdp_table->entries[pdp_idx]);
        if (!pd_table)
            return;

        // --- Level 2 (PD) ---
        // ★ Huge Page 衝突時の分割処理 (Split) ★
        if (pd_table->entries[pd_idx].bits.present && pd_table->entries[pd_idx].bits.huge_page)
        {
            // 1. 新しいページテーブル(PT)を作成
            PageTable *new_pt = AllocateTable();
            if (!new_pt)
                return; // メモリ不足

            // 2. Huge Pageの中身(2MB分)を、512個の4KBエントリとしてコピー
            uint64_t huge_base_phys = pd_table->entries[pd_idx].GetAddress();
            uint64_t huge_flags = pd_table->entries[pd_idx].value & 0xFFF; // 下位フラグを保持

            for (int k = 0; k < 512; ++k)
            {
                new_pt->entries[k].SetAddress(huge_base_phys + (k * kPageSize4K));

                // フラグをコピー (Hugeビットは落とす)
                // valueに直接書き込むことで属性を引き継ぐ
                new_pt->entries[k].value |= huge_flags;
                new_pt->entries[k].bits.huge_page = 0;
                new_pt->entries[k].bits.present = 1;
            }

            // 3. PDエントリを、作成したPTに向ける
            // Hugeビットを落とし、PTへのポインタをセット
            pd_table->entries[pd_idx].SetAddress(reinterpret_cast<uint64_t>(new_pt));
            pd_table->entries[pd_idx].bits.huge_page = 0;
            pd_table->entries[pd_idx].bits.present = 1;
            // PD自体もUser権限が必要なら付与する
            if (flags & kUser)
            {
                pd_table->entries[pd_idx].bits.user_supervisor = 1;
            }

            // ログ出し (デバッグ用)
            // kprintf("[Paging] Split Huge Page at PD[%ld] (Virt ~%lx)\n", pd_idx, vaddr & ~0x1FFFFF);
        }

        PageTable *pt_table = EnsureEntry(pd_table->entries[pd_idx]);
        if (!pt_table)
            return;

        // --- Level 1 (PT) ---
        // ここで対象のページだけ新しい設定で上書きされる
        pt_table->entries[pt_idx].SetAddress(paddr);
        pt_table->entries[pt_idx].bits.present = (flags & kPresent) ? 1 : 0;
        pt_table->entries[pt_idx].bits.read_write = (flags & kWritable) ? 1 : 0;
        pt_table->entries[pt_idx].bits.user_supervisor = (flags & kUser) ? 1 : 0;

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
    pml4_table_->entries[0].bits.user_supervisor = 1; // ユーザーモードからもアクセス可

    // 64個のPDテーブルを作成して、64GB分 (64 * 1GB) をマップする
    for (int i_pdp = 0; i_pdp < 64; ++i_pdp)
    {
        PageTable *pd_table = AllocateTable();
        pdp_table->entries[i_pdp].SetAddress(reinterpret_cast<uint64_t>(pd_table));
        pdp_table->entries[i_pdp].bits.present = 1;
        pdp_table->entries[i_pdp].bits.read_write = 1;
        pdp_table->entries[i_pdp].bits.user_supervisor = 1; // ユーザーモードからもアクセス可

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
            pd_table->entries[i_pd].bits.huge_page = 1;       // 2MBページ
            pd_table->entries[i_pd].bits.user_supervisor = 1; // ユーザーモードからもアクセス可
        }
    }

    kprintf("[Paging] Identity Mapping (0-64GB) Created.\n");

    // 3. CR3ロード (ページング有効化)
    LoadCR3(reinterpret_cast<uint64_t>(pml4_table_));

    kprintf("[Paging] CR3 Loaded. Paging is active!\n");
}