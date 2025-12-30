#include "paging.hpp"
#include "arch/inasm.hpp"
#include "cxx.hpp" // memset用
#include "memory/memory_manager.hpp"
#include "printk.hpp" // デバッグ用
#include <stddef.h>

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

bool PageManager::AllocateVirtual(uint64_t virtual_addr, size_t size,
                                  uint64_t flags)
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

void PageManager::MapPage(uint64_t virtual_addr, uint64_t physical_addr,
                          size_t count, uint64_t flags)
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
            if (!entry.IsPresent())
            {
                PageTable *new_table = AllocateTable();
                if (!new_table)
                    return nullptr;

                // 中間テーブルとして作成
                // flagsに含まれる権限(User/Writable)を引き継ぐべきか？
                // x86では中間テーブルのU/Sビットが0だと、末端がUでもアクセスできない。
                // なので、とりあえず親切に権限を与えておく。
                entry.Set(reinterpret_cast<uint64_t>(new_table),
                          PageTableEntry::Type::Table,
                          flags | PageManager::kPresent |
                              PageManager::kWritable | PageManager::kUser);
            }
            else
            {
                if (flags & kUser)
                {
                    entry.SetUserAccess(true);
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
        if (pd_table->entries[pd_idx].IsPresent() &&
            pd_table->entries[pd_idx].IsHugePage())
        {
            // 1. 新しいページテーブル(PT)を作成
            PageTable *new_pt = AllocateTable();
            if (!new_pt)
                return; // メモリ不足

            // 2. Huge Pageの中身(2MB分)を、512個の4KBエントリとしてコピー
            uint64_t huge_base_phys = pd_table->entries[pd_idx].GetAddress();

            // 元の属性を保持するためにエントリのコピーを取る
            PageTableEntry source_entry = pd_table->entries[pd_idx];

            for (int k = 0; k < 512; ++k)
            {
                // アドレスとタイプ(Page)を設定
                new_pt->entries[k].Set(huge_base_phys + (k * kPageSize4K),
                                       PageTableEntry::Type::Page, 0);

                // 元の属性をコピー
                new_pt->entries[k].CopyAttributesFrom(source_entry);
            }

            // 3. PDエントリを、作成したPTに向ける
            // Tableタイプとして再設定、属性は継承（CopyAttributesFromは使えないので再設定）
            // 中間テーブルなのでフル権限にしておく
            pd_table->entries[pd_idx].Set(reinterpret_cast<uint64_t>(new_pt),
                                          PageTableEntry::Type::Table,
                                          flags | kPresent | kWritable | kUser);

            // ログ出し (デバッグ用)
            // kprintf("[Paging] Split Huge Page at PD[%ld] (Virt ~%lx)\n",
            // pd_idx, vaddr & ~0x1FFFFF);
        }

        PageTable *pt_table = EnsureEntry(pd_table->entries[pd_idx]);
        if (!pt_table)
            return;

        // --- Level 1 (PT) ---
        // ここで対象のページだけ新しい設定で上書きされる
        pt_table->entries[pt_idx].Set(paddr, PageTableEntry::Type::Page, flags);

        InvalidateTLB(vaddr);
    }
}

void PageManager::Initialize()
{
    kprintf("[Paging] Initializing with 2MB Huge Pages...\n");

#if defined(__aarch64__)
    // Temporary Workaround: Avoid Kernel Stack Collision
    // The kernel stack is located around 0x40010000 (set by loader).
    // The default MemoryManager might return the same address for the first
    // allocated frame (PML4). To avoid corruption, we allocate and discard a
    // few initial frames to shift the PML4 address.
    for (int i = 0; i < 32; ++i)
    {
        MemoryManager::AllocateFrame();
    }
#endif

    // 1. ルートテーブル (PML4) 作成
    pml4_table_ = reinterpret_cast<PML4Table *>(AllocateTable());
    if (!pml4_table_)
        while (1)
            Hlt();

    // 2. アイデンティティマッピング (0-64GB)
    // ビデオメモリ(VRAM)やMMIOデバイスが 3GB-4GB 付近やそれ以上にあるため、
    // 広範囲をカバーしておく必要がある。

    // PDPテーブル (512GB分をカバー) を1つ作成
    // 仮想アドレス 0x000... は PML4[0] に対応
    PageTable *pdp_table = AllocateTable();
    pml4_table_->entries[0].Set(reinterpret_cast<uint64_t>(pdp_table),
                                PageTableEntry::Type::Table,
                                kPresent | kWritable | kUser);
#if defined(__aarch64__)
    // キャッシュをクリーンしてメモリに書き戻す
    __asm__ volatile("dc cvac, %0"
                     :
                     : "r"(&pml4_table_->entries[0])
                     : "memory");
#endif

    // 64個のPDテーブルを作成して、64GB分 (64 * 1GB) をマップする
    for (int i_pdp = 0; i_pdp < 64; ++i_pdp)
    {
        PageTable *pd_table = AllocateTable();
        pdp_table->entries[i_pdp].Set(reinterpret_cast<uint64_t>(pd_table),
                                      PageTableEntry::Type::Table,
                                      kPresent | kWritable | kUser);

        // PDエントリを埋める (各エントリ 2MB * 512 = 1GB)
        for (int i_pd = 0; i_pd < 512; ++i_pd)
        {
            // 物理アドレス計算
            uint64_t physical_addr =
                (static_cast<uint64_t>(i_pdp) * 1024 * 1024 * 1024) +
                (static_cast<uint64_t>(i_pd) * 2 * 1024 * 1024);

            // Kernel Only Mapping (kUser removed)
            // EL1で動作するカーネルは、自身のコード/データをUserアクセス可能にする必要はない。
            // また、PAN(Privileged Access
            // Never)の影響を避けるためにもKernel属性が適切。
#if defined(__aarch64__)
            uint64_t flags = kPresent | kWritable;
#else
            // x86_64ではアプリがIdentity
            // Mapping領域にアクセスする場合があるため、kUserが必要
            uint64_t flags = kPresent | kWritable | kUser;
#endif
#if defined(__aarch64__)
            // QEMU virtマシンのメモリマップでは、RAMは0x40000000から始まる。
            // 0-1GB未満はI/O領域やFlashなどが含まれるため、Device属性(nGnRnE)にする必要がある。
            // Normal
            // Memoryとしてアクセスすると、投機実行などで不正アクセス例外が発生する。
            if (physical_addr < 0x40000000)
            {
                flags |= kDevice;
            }
#endif
            pd_table->entries[i_pd].Set(physical_addr,
                                        PageTableEntry::Type::Block, flags);
#if defined(__aarch64__)
            __asm__ volatile("dc cvac, %0"
                             :
                             : "r"(&pd_table->entries[i_pd])
                             : "memory");
#endif
        }
    }

    kprintf("[Paging] Identity Mapping (0-64GB) Created.\n");

    // 3. CR3ロード (ページング有効化)
    LoadCR3(reinterpret_cast<uint64_t>(pml4_table_));

    kprintf("[Paging] CR3 Loaded. Paging is active!\n");
}

// =========================================
// プロセス用ページテーブル管理 (Ring 3対応)
// =========================================

uint64_t PageManager::GetKernelCR3()
{
    return reinterpret_cast<uint64_t>(pml4_table_);
}

// ページテーブルのコピー（Shallow Copy）
PageTable *PageManager::CopyPageTable(PageTable *src, int level)
{
    PageTable *new_table = AllocateTable();
    if (!new_table)
        return nullptr;

    for (int i = 0; i < 512; ++i)
    {
        new_table->entries[i] = src->entries[i];
    }
    return new_table;
}

// ページテーブル階層の解放
void PageManager::FreePageTableHierarchy(PageTable *table, int level,
                                         bool free_frames)
{
    if (!table)
        return;

    for (int i = 0; i < 512; ++i)
    {
        auto &entry = table->entries[i];
        if (!entry.IsPresent())
            continue;

        uint64_t child_addr = entry.GetAddress();
        if (child_addr == 0)
            continue;

        if (level > 1) // PML4, PDP, PD
        {
            // Huge Pageの場合、それ以下はないので物理フレーム解放のみ
            if (entry.IsHugePage())
            {
                if (free_frames)
                {
                    // 2MBページ解放 (実装略、MemoryManager側が対応必要)
                    // MemoryManager::FreeFrame(reinterpret_cast<void*>(child_addr));
                    // 簡易的に先頭フレームだけ解放しておく（本来は512回FreeFrameが必要）
                    // 今はHugePageをSplitしたPTを使っているはずなのでここはあまり通らない
                }
            }
            else
            {
                // 再帰的に解放
                PageTable *child_table =
                    reinterpret_cast<PageTable *>(child_addr);
                FreePageTableHierarchy(child_table, level - 1, free_frames);
                // 子テーブル自体を解放
                MemoryManager::FreeFrame(child_table);
            }
        }
        else // PT (Level 1)
        {
            if (free_frames)
            {
                // 物理フレームを解放
                MemoryManager::FreeFrame(reinterpret_cast<void *>(child_addr));
            }
        }
    }
}

uint64_t PageManager::CreateProcessPageTable()
{
    // 1. 新しいPML4テーブルを作成
    PML4Table *new_pml4 = CopyPageTable(pml4_table_, 4);
    if (!new_pml4)
    {
        kprintf("[Paging] Failed to allocate PML4 for new process\n");
        return 0;
    }

    // 2. ユーザー領域 (PDP[0]の範囲、特に0x40000000以降) を分離するために
    // PDP[0] を複製する (Deep Copy Level 1)
    if (new_pml4->entries[0].IsPresent())
    {
        PageTable *src_pdp =
            reinterpret_cast<PageTable *>(new_pml4->entries[0].GetAddress());
        PageTable *new_pdp = CopyPageTable(src_pdp, 3);

        if (new_pdp)
        {
            // 新しいPDPをPML4にセット
            new_pml4->entries[0].SetAddress(
                reinterpret_cast<uint64_t>(new_pdp));

            // 3. アプリロード領域A (0x00000000~0x3FFFFFFF = PD[0]) を複製
            // ここにアプリのコードが配置されることが多い
            if (new_pdp->entries[0].IsPresent())
            {
                PageTable *src_pd = reinterpret_cast<PageTable *>(
                    new_pdp->entries[0].GetAddress());
                PageTable *new_pd = CopyPageTable(src_pd, 2);

                if (new_pd)
                {
                    new_pdp->entries[0].SetAddress(
                        reinterpret_cast<uint64_t>(new_pd));
                }
            }

            // 4. アプリロード領域B (0x40000000~0x7FFFFFFF = PD[1]) を複製 (Deep
            // Copy Level 2)
            // これにより、アプリ領域のページ操作がカーネル共有のPD[1]に影響しなくなる
            if (new_pdp->entries[1].IsPresent())
            {
                PageTable *src_pd = reinterpret_cast<PageTable *>(
                    new_pdp->entries[1].GetAddress());
                PageTable *new_pd = CopyPageTable(src_pd, 2);

                if (new_pd)
                {
                    new_pdp->entries[1].SetAddress(
                        reinterpret_cast<uint64_t>(new_pd));
                }
            }
        }
    }

    uint64_t new_cr3 = reinterpret_cast<uint64_t>(new_pml4);

    return new_cr3;
}

void PageManager::SwitchPageTable(uint64_t cr3_value)
{
    if (cr3_value == 0)
    {
        kprintf("[Paging] Warning: Attempted to switch to null page table\n");
        return;
    }
    LoadCR3(cr3_value);
}

void PageManager::FreeProcessPageTable(uint64_t cr3_value)
{
    // カーネルのページテーブルは解放できない
    if (cr3_value == reinterpret_cast<uint64_t>(pml4_table_))
    {
        kprintf("[Paging] Warning: Cannot free kernel page table\n");
        return;
    }

    if (cr3_value == 0)
        return;

    PML4Table *target_pml4 = reinterpret_cast<PML4Table *>(cr3_value);

    // ユーザープロセス用に複製した領域を解放

    // 1. PDP[0]を取得
    if (target_pml4->entries[0].IsPresent())
    {
        PageTable *target_pdp =
            reinterpret_cast<PageTable *>(target_pml4->entries[0].GetAddress());

        // 2. PD[0] (アプリコード領域) を取得して解放
        // 注意: PD[0]の中のエントリ（PT/フレーム）はカーネルと共有しているため
        // 再帰的に解放するとカーネルが壊れる！PDテーブル自体のみ解放する。
        if (target_pdp->entries[0].IsPresent())
        {
            PageTable *target_pd = reinterpret_cast<PageTable *>(
                target_pdp->entries[0].GetAddress());

            // PD[0]テーブル自体を解放（中身は共有なので触らない！）
            MemoryManager::FreeFrame(target_pd);

            // ポインタクリア
            target_pdp->entries[0].value = 0;
        }

        // 3. PD[1] (アプリスタック領域) を取得して解放
        if (target_pdp->entries[1].IsPresent())
        {
            PageTable *target_pd = reinterpret_cast<PageTable *>(
                target_pdp->entries[1].GetAddress());

            // PD[1]テーブル自体を解放（中身は共有なので触らない！）
            MemoryManager::FreeFrame(target_pd);

            // ポインタクリア
            target_pdp->entries[1].value = 0;
        }

        // PDP[0]テーブル自体を解放 (他のエントリは共有なので中身は解放しない！)
        MemoryManager::FreeFrame(target_pdp);
    }

    // PML4自体を解放
    MemoryManager::FreeFrame(target_pml4);
}

bool PageManager::AllocateVirtualForProcess(uint64_t target_cr3,
                                            uint64_t virtual_addr, size_t size,
                                            uint64_t flags)
{
    // 現在のCR3を保存
    uint64_t current_cr3 = GetCR3();

    // 対象のページテーブルに切り替え
    if (target_cr3 != current_cr3)
    {
        SwitchPageTable(target_cr3);
    }

    // 一時的にpml4_table_を対象のものに差し替え
    PML4Table *original_pml4 = pml4_table_;
    pml4_table_ = reinterpret_cast<PML4Table *>(target_cr3);

    // 通常のAllocateVirtualを呼び出す
    bool result = AllocateVirtual(virtual_addr, size, flags);

    // pml4_table_を元に戻す
    pml4_table_ = original_pml4;

    // 元のページテーブルに戻す
    if (target_cr3 != current_cr3)
    {
        SwitchPageTable(current_cr3);
    }

    return result;
}