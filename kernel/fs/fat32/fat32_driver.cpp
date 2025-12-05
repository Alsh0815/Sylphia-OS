#include "cxx.hpp"
#include "driver/nvme/nvme_driver.hpp"
#include "memory/memory_manager.hpp"
#include "printk.hpp"
#include <std/string.hpp>

#include "fat32_driver.hpp"

namespace FileSystem
{
FAT32Driver *g_fat32_driver = nullptr;
FAT32Driver *g_system_fs = nullptr;

FAT32Driver::FAT32Driver(BlockDevice *dev, uint64_t partition_lba)
    : dev_(dev), part_lba_(partition_lba)
{
}

void FAT32Driver::Initialize()
{
    // BPB (LBA 0) を読み込む
    uint8_t *buf = static_cast<uint8_t *>(MemoryManager::Allocate(512, 4096));
    dev_->Read(part_lba_, buf, 1);

    FAT32_BPB *bpb = reinterpret_cast<FAT32_BPB *>(buf);

    kprintf("[FAT32 DEBUG] Read BPB from LBA %lld\n", part_lba_);
    kprintf("[FAT32 DEBUG] Signature: %x (Expect 0xAA55)\n", bpb->signature);
    kprintf("[FAT32 DEBUG] SecPerClus: %d\n", bpb->sec_per_clus);
    kprintf("[FAT32 DEBUG] ResSectors: %d\n", bpb->reserved_sec_cnt);

    // パラメータ取得
    sec_per_clus_ = bpb->sec_per_clus;
    reserved_sectors_ = bpb->reserved_sec_cnt;
    num_fats_ = bpb->num_fats;
    fat_sz32_ = bpb->fat_sz32;
    root_clus_ = bpb->root_clus;

    // 領域開始位置の計算
    fat_start_lba_ = part_lba_ + reserved_sectors_;
    data_start_lba_ = fat_start_lba_ + (num_fats_ * fat_sz32_);

    kprintf("[FAT32] Driver Initialized. ClusterSize=%d sectors\n",
            sec_per_clus_);
    MemoryManager::Free(buf, 512);
}

uint64_t FAT32Driver::ClusterToLBA(uint32_t cluster)
{
    // クラスタ2がデータ領域の先頭
    return data_start_lba_ + (uint64_t)(cluster - 2) * sec_per_clus_;
}

uint32_t FAT32Driver::AllocateCluster()
{
    // FATテーブルを走査して空き(0x00000000)を探す
    // ※本来はFSInfoを見て高速化すべきですが、今回はFATを先頭から読みます

    uint8_t *buf = static_cast<uint8_t *>(MemoryManager::Allocate(512, 4096));
    uint32_t *entries = reinterpret_cast<uint32_t *>(buf);

    // クラスタ2から探索開始 (FAT領域の先頭セクタだけ見る簡易実装)
    // ※本気でやるならFAT領域全体をループする必要があります
    dev_->Read(fat_start_lba_, buf, 1);

    for (int i = 2; i < 128; ++i)
    { // 1セクタには128個のエントリ (512/4)
        if (entries[i] == 0)
        {
            // 空き発見！使用中(EOCC = 0x0FFFFFFF)マークをつけて保存
            entries[i] = 0x0FFFFFFF;
            dev_->Write(fat_start_lba_, buf, 1);
            // FAT2(バックアップ)も更新
            dev_->Write(fat_start_lba_ + fat_sz32_, buf, 1);

            MemoryManager::Free(buf, 512);
            return i;
        }
    }

    MemoryManager::Free(buf, 512);
    kprintf("[FAT32] No free clusters found in first FAT sector!\n");
    return 0; // Error
}

void FAT32Driver::LinkCluster(uint32_t current, uint32_t next)
{
    // FATテーブル内の current の位置に next を書き込む
    // 1セクタ = 128エントリ
    uint32_t sector_offset = current / 128;
    uint32_t entry_offset = current % 128;

    uint64_t target_lba = fat_start_lba_ + sector_offset;

    // セクタを読み込む
    uint8_t *buf = static_cast<uint8_t *>(MemoryManager::Allocate(512, 4096));
    dev_->Read(target_lba, buf, 1);

    uint32_t *entries = reinterpret_cast<uint32_t *>(buf);
    entries[entry_offset] = next; // リンク更新

    // 書き戻す
    dev_->Write(target_lba, buf, 1);

    // バックアップFAT(FAT2)も更新
    dev_->Write(target_lba + fat_sz32_, buf, 1);

    MemoryManager::Free(buf, 512);
}

uint32_t FAT32Driver::GetNextCluster(uint32_t current_cluster)
{
    uint32_t sector_offset = current_cluster / 128; // 1セクタに128エントリ
    uint32_t entry_offset = current_cluster % 128;

    uint64_t target_lba = fat_start_lba_ + sector_offset;

    uint8_t *buf = static_cast<uint8_t *>(MemoryManager::Allocate(512, 4096));
    dev_->Read(target_lba, buf, 1);

    uint32_t *entries = reinterpret_cast<uint32_t *>(buf);
    uint32_t next = entries[entry_offset] & 0x0FFFFFFF; // 下位28ビットが有効

    MemoryManager::Free(buf, 512);
    return next;
}

void FAT32Driver::FreeChain(uint32_t start_cluster)
{
    // クラスタ番号が有効範囲にある限りループ
    uint32_t current = start_cluster;
    while (current >= 2 && current < 0x0FFFFFF8)
    {
        // 次のクラスタを覚えておく
        uint32_t next = GetNextCluster(current);

        // 現在のクラスタを「空き(0)」にする
        // LinkCluster(current, 0)
        // は「currentの位置に0を書く」ので解放と同じ意味
        LinkCluster(current, 0);

        // 次へ進む
        current = next;
    }
}

bool FAT32Driver::IsNameEqual(const char *entry_name, const char *target_name)
{
    // entry_name: "KERNEL  ELF" (11文字固定, スペース埋め)
    // target_name: "kernel.elf" (ユーザ入力)

    char converted[11];
    memset(converted, ' ', 11);

    int i = 0; // target_name index
    int j = 0; // converted index

    // 1. ベース名部分 (ドットの前まで)
    while (target_name[i] && target_name[i] != '.')
    {
        if (j < 8)
        {
            char c = target_name[i];
            // 小文字 -> 大文字変換
            if (c >= 'a' && c <= 'z')
                c -= 32;
            converted[j++] = c;
        }
        i++;
    }

    // 2. ドットがあれば拡張子部分へ
    if (target_name[i] == '.')
    {
        i++;   // ドットをスキップ
        j = 8; // 拡張子エリアへ移動
        while (target_name[i])
        {
            if (j < 11)
            {
                char c = target_name[i];
                if (c >= 'a' && c <= 'z')
                    c -= 32;
                converted[j++] = c;
            }
            i++;
        }
    }

    // 3. 比較
    for (int k = 0; k < 11; ++k)
    {
        if (entry_name[k] != converted[k])
            return false;
    }
    return true;
}

void FAT32Driver::AddDirectoryEntry(const char *name, uint32_t start_cluster,
                                    uint32_t size, uint8_t attr,
                                    uint32_t parent_cluster)
{
    // 親ディレクトリの開始クラスタを決定 (0ならルート)
    uint32_t target_cluster =
        (parent_cluster == 0) ? root_clus_ : parent_cluster;
    uint64_t lba = ClusterToLBA(target_cluster);

    uint8_t *buf = static_cast<uint8_t *>(MemoryManager::Allocate(512, 4096));

    // 親ディレクトリの中身を走査 (簡易的に先頭クラスタのみ探索)
    for (int s = 0; s < sec_per_clus_; ++s)
    {
        dev_->Read(lba + s, buf, 1);
        DirectoryEntry *dir = reinterpret_cast<DirectoryEntry *>(buf);

        for (int i = 0; i < 16; ++i)
        {
            if (dir[i].name[0] == 0x00 || (unsigned char)dir[i].name[0] == 0xE5)
            {
                // エントリ作成
                memset(&dir[i], 0, 32);
                memcpy(dir[i].name, name, 11);
                dir[i].attr = attr; // [修正] 引数の属性を使う (ファイル:0x20,
                                    // ディレクトリ:0x10)
                dir[i].fst_clus_hi = (start_cluster >> 16) & 0xFFFF;
                dir[i].fst_clus_lo = start_cluster & 0xFFFF;
                dir[i].file_size = size;

                dev_->Write(lba + s, buf, 1);
                MemoryManager::Free(buf, 512);
                return;
            }
        }
    }
    MemoryManager::Free(buf, 512);
    kprintf("[FAT32] Directory full!\n");
}

bool FAT32Driver::FindDirectoryEntry(const char *name, uint32_t parent_cluster,
                                     DirectoryEntry *found_entry)
{
    uint32_t current_cluster =
        (parent_cluster == 0) ? root_clus_ : parent_cluster;
    uint8_t *buf = static_cast<uint8_t *>(
        MemoryManager::Allocate(sec_per_clus_ * 512, 4096));

    int loop_safety = 0;
    while (current_cluster < 0x0FFFFFF8 && current_cluster >= 2)
    {
        if (loop_safety++ > 10000)
        {
            kprintf("[FAT32] Error: Directory cluster chain too long or loop "
                    "detected.\n");
            break;
        }

        uint64_t lba = ClusterToLBA(current_cluster);

        if (!dev_->Read(lba, buf, sec_per_clus_))
        {
            kprintf("[FAT32] Disk Read Error at LBA %lld\n", lba);
            MemoryManager::Free(buf, sec_per_clus_ * 512);
            return false;
        }

        DirectoryEntry *entries = reinterpret_cast<DirectoryEntry *>(buf);
        int num_entries = (sec_per_clus_ * 512) / 32;

        for (int i = 0; i < num_entries; ++i)
        {
            // 0x00: これ以降エントリなし
            if (entries[i].name[0] == 0x00)
            {
                MemoryManager::Free(buf, sec_per_clus_ * 512);
                return false;
            }
            // 0xE5: 削除済み
            if ((unsigned char)entries[i].name[0] == 0xE5)
                continue;

            // 属性チェック (ボリュームラベル等はスキップ)
            if (entries[i].attr == 0x0F)
                continue; // LFN (Long File Name) スキップ

            if (IsNameEqual(entries[i].name, name))
            {
                *found_entry = entries[i]; // コピーして返す
                MemoryManager::Free(buf, sec_per_clus_ * 512);
                return true;
            }
        }

        // 次のクラスタへ (ディレクトリが複数クラスタにまたがる場合)
        current_cluster = GetNextCluster(current_cluster);
    }

    MemoryManager::Free(buf, sec_per_clus_ * 512);
    return false;
}

uint32_t FAT32Driver::CreateDirectory(const char *name, uint32_t parent_cluster)
{
    kprintf("[FAT32] Creating Directory: %s...\n", name);

    // 1. ディレクトリ用のクラスタを確保
    uint32_t new_cluster = AllocateCluster();
    if (new_cluster == 0)
        return 0;

    // 2. 確保したクラスタを0クリア (空のディレクトリにする)
    uint64_t target_lba = ClusterToLBA(new_cluster);
    uint32_t cluster_bytes = sec_per_clus_ * 512;
    uint8_t *buf =
        static_cast<uint8_t *>(MemoryManager::Allocate(cluster_bytes, 4096));
    memset(buf, 0, cluster_bytes);

    DirectoryEntry *dot_entries = reinterpret_cast<DirectoryEntry *>(buf);

    // Entry 0: "." (自分自身)
    memset(&dot_entries[0], 0, 32);
    memcpy(dot_entries[0].name, ".          ", 11); // "." + 10 spaces
    dot_entries[0].attr = 0x10;                     // Directory
    dot_entries[0].fst_clus_hi = (new_cluster >> 16) & 0xFFFF;
    dot_entries[0].fst_clus_lo = new_cluster & 0xFFFF;

    // Entry 1: ".." (親ディレクトリ)
    memset(&dot_entries[1], 0, 32);
    memcpy(dot_entries[1].name, "..         ", 11); // ".." + 9 spaces
    dot_entries[1].attr = 0x10;                     // Directory
    // 親がルート(0)の場合はクラスタ0を指定 (FAT32の仕様)
    uint32_t parent_ref = (parent_cluster == 0) ? 0 : parent_cluster;
    dot_entries[1].fst_clus_hi = (parent_ref >> 16) & 0xFFFF;
    dot_entries[1].fst_clus_lo = parent_ref & 0xFFFF;

    dev_->Write(target_lba, buf, sec_per_clus_);
    MemoryManager::Free(buf, cluster_bytes);

    // 3. FATチェーン終端
    LinkCluster(new_cluster, 0x0FFFFFFF);

    // 4. 親ディレクトリにこのディレクトリのエントリを追加
    // 属性 0x10 (Directory), サイズ 0
    AddDirectoryEntry(name, new_cluster, 0, 0x10, parent_cluster);

    return new_cluster;
}

uint32_t FAT32Driver::EnsureDirectory(const char *path)
{
    uint32_t current_cluster = root_clus_;

    const char *p = path;
    if (*p == '/')
        p++;

    char segment[16];

    while (*p)
    {
        int i = 0;
        while (*p && *p != '/' && i < 15)
        {
            segment[i++] = *p++;
        }
        segment[i] = '\0';
        if (*p == '/')
            p++;

        if (i == 0)
            break;

        DirectoryEntry entry;
        if (FindDirectoryEntry(segment, current_cluster, &entry))
        {
            if (entry.attr & 0x10)
            {
                current_cluster = (entry.fst_clus_hi << 16) | entry.fst_clus_lo;
            }
            else
            {
                kprintf("[FAT32] Error: %s is a file, not a directory.\n",
                        segment);
                return 0;
            }
        }
        else
        {
            char name83[11];
            To83Format(segment, name83);

            uint32_t new_cluster = CreateDirectory(name83, current_cluster);
            if (new_cluster == 0)
                return 0;

            current_cluster = new_cluster;
        }
    }

    return current_cluster;
}

void FAT32Driver::ListDirectory(uint32_t cluster)
{
    uint32_t current_cluster = (cluster == 0) ? root_clus_ : cluster;
    uint32_t cluster_bytes = sec_per_clus_ * 512;
    uint8_t *buf =
        static_cast<uint8_t *>(MemoryManager::Allocate(cluster_bytes, 4096));

    kprintf("Type     Size       Name\n");
    kprintf("----     ----       ----\n");

    while (current_cluster < 0x0FFFFFF8)
    {
        dev_->Read(ClusterToLBA(current_cluster), buf, sec_per_clus_);
        DirectoryEntry *entries = reinterpret_cast<DirectoryEntry *>(buf);
        int num_entries = cluster_bytes / 32;

        for (int i = 0; i < num_entries; ++i)
        {
            if (entries[i].name[0] == 0x00)
                break;
            if ((unsigned char)entries[i].name[0] == 0xE5)
                continue;
            if (entries[i].attr == 0x0F)
                continue; // LFNスキップ

            // 名前を整形 ("KERNEL  ELF" -> "KERNEL.ELF")
            char name_buf[13];
            int idx = 0;
            // Base
            for (int k = 0; k < 8; ++k)
            {
                if (entries[i].name[k] != ' ')
                    name_buf[idx++] = entries[i].name[k];
            }
            // Ext (ディレクトリ以外なら)
            if (!(entries[i].attr & 0x10) && entries[i].name[8] != ' ')
            {
                name_buf[idx++] = '.';
                for (int k = 8; k < 11; ++k)
                {
                    if (entries[i].name[k] != ' ')
                        name_buf[idx++] = entries[i].name[k];
                }
            }
            name_buf[idx] = '\0';

            if (entries[i].attr & 0x10)
                kprintf("DIR      ");
            else
                kprintf("FILE     ");

            // サイズ表示 (簡易整列)
            char size_str[16];
            // ※sprintfがないので簡易表示
            kprintf("%d ", entries[i].file_size);

            // 位置合わせのためのスペース
            if (entries[i].file_size < 1000000)
                kprintf(" ");
            if (entries[i].file_size < 1000)
                kprintf("   ");

            kprintf("%s\n", name_buf);
        }
        current_cluster = GetNextCluster(current_cluster);
    }
    MemoryManager::Free(buf, cluster_bytes);
}

uint32_t FAT32Driver::GetDirectoryCluster(const char *path,
                                          uint32_t base_cluster)
{
    DirectoryEntry entry;
    if (strcmp(path, "/") == 0)
        return root_clus_;
    if (path[0] == 0 || strcmp(path, ".") == 0)
        return base_cluster == 0 ? root_clus_ : base_cluster;

    if (GetFileEntry(path, &entry, base_cluster))
    {
        if (entry.attr & 0x10)
        {
            uint32_t clus = (entry.fst_clus_hi << 16) | entry.fst_clus_lo;
            return (clus == 0) ? root_clus_ : clus;
        }
    }
    return 0xFFFFFFFF;
}

bool FAT32Driver::GetFileEntry(const char *path, DirectoryEntry *ret_entry,
                               uint32_t base_cluster)
{
    uint32_t current_cluster = base_cluster;
    const char *p = path;

    if (*p == '/')
    {
        current_cluster = root_clus_;
        p++;
    }
    else if (current_cluster == 0)
    {
        current_cluster = root_clus_;
    }

    char name_buf[16];

    while (*p)
    {
        int i = 0;
        while (*p && *p != '/' && i < 15)
        {
            name_buf[i++] = *p++;
        }
        name_buf[i] = 0;

        DirectoryEntry entry;
        if (!FindDirectoryEntry(name_buf, current_cluster, &entry))
        {
            return false;
        }

        if (*p == 0)
        {
            *ret_entry = entry;
            return true;
        }

        if (!(entry.attr & 0x10))
        {
            return false;
        }
        current_cluster = (entry.fst_clus_hi << 16) | entry.fst_clus_lo;
        if (current_cluster == 0)
            current_cluster = root_clus_;

        if (*p == '/')
            p++;
    }
    return false;
}

uint32_t FAT32Driver::GetFileSize(const char *path)
{
    DirectoryEntry entry;
    if (GetFileEntry(path, &entry))
    {
        return entry.file_size;
    }
    return 0;
}

bool FAT32Driver::DeleteFile(const char *name, uint32_t parent_cluster)
{
    kprintf("[FAT32] Deleting file: %s...\n", name);

    uint32_t current_cluster =
        (parent_cluster == 0) ? root_clus_ : parent_cluster;
    uint8_t *buf = static_cast<uint8_t *>(
        MemoryManager::Allocate(sec_per_clus_ * 512, 4096));

    // ディレクトリ内を検索
    while (current_cluster < 0x0FFFFFF8)
    {
        uint64_t lba = ClusterToLBA(current_cluster);

        // 1クラスタ分読み込み
        dev_->Read(lba, buf, sec_per_clus_);

        DirectoryEntry *entries = reinterpret_cast<DirectoryEntry *>(buf);
        int num_entries = (sec_per_clus_ * 512) / 32;
        bool found = false;
        int found_index = -1;

        for (int i = 0; i < num_entries; ++i)
        {
            if (entries[i].name[0] == 0x00)
                break; // これ以上なし
            if ((unsigned char)entries[i].name[0] == 0xE5)
                continue; // 既に削除済み
            if (entries[i].attr == 0x0F)
                continue; // LFN

            if (IsNameEqual(entries[i].name, name))
            {
                found = true;
                found_index = i;
                break;
            }
        }

        if (found)
        {
            // 1. ファイルの実体(クラスタ)を解放する
            uint32_t fst_clus = (entries[found_index].fst_clus_hi << 16) |
                                entries[found_index].fst_clus_lo;
            if (fst_clus != 0)
            {
                FreeChain(fst_clus);
            }

            // 2. ディレクトリエントリを「削除済み(0xE5)」にマークする
            entries[found_index].name[0] = 0xE5;

            // 3. ディスクに書き戻す

            // found_index はバッファ全体の何番目か。
            // 1セクタ=16エントリ(512/32)。
            // 該当するセクタのオフセットを計算。
            uint32_t sector_offset = found_index / 16;

            // バッファ内の該当セクタの先頭ポインタ
            uint8_t *sector_ptr = buf + (sector_offset * 512);

            // 書き込み (LBA + セクタオフセット)
            dev_->Write(lba + sector_offset, sector_ptr, 1);

            kprintf("[FAT32] File deleted.\n");
            MemoryManager::Free(buf, sec_per_clus_ * 512);
            return true;
        }

        // 次のクラスタへ
        current_cluster = GetNextCluster(current_cluster);
    }

    MemoryManager::Free(buf, sec_per_clus_ * 512);
    kprintf("[FAT32] File not found.\n");
    return false;
}

uint32_t FAT32Driver::ReadFile(const char *name, void *buffer,
                               uint32_t buffer_size, uint32_t base_cluster)
{
    DirectoryEntry entry;
    if (!GetFileEntry(name, &entry, base_cluster))
        return 0;

    if (entry.file_size > buffer_size)
    {
        kprintf("[FAT32] Error: Buffer too small (%d < %d)\n", buffer_size,
                entry.file_size);
        return 0;
    }

    uint32_t current_cluster = (entry.fst_clus_hi << 16) | entry.fst_clus_lo;
    uint32_t bytes_remaining = entry.file_size;
    uint32_t offset = 0;
    uint8_t *out_ptr = static_cast<uint8_t *>(buffer);
    uint32_t cluster_bytes = sec_per_clus_ * 512;

    // 作業用バッファ (NVMeのアライメント要件を満たすため)
    uint8_t *temp_buf =
        static_cast<uint8_t *>(MemoryManager::Allocate(cluster_bytes, 4096));

    while (bytes_remaining > 0 && current_cluster < 0x0FFFFFF8)
    {
        uint64_t lba = ClusterToLBA(current_cluster);

        // 1クラスタ読み込み
        dev_->Read(lba, temp_buf, sec_per_clus_);

        // 必要な分だけコピー
        uint32_t copy_len =
            (bytes_remaining > cluster_bytes) ? cluster_bytes : bytes_remaining;
        memcpy(out_ptr + offset, temp_buf, copy_len);

        offset += copy_len;
        bytes_remaining -= copy_len;

        // 次のクラスタへ
        current_cluster = GetNextCluster(current_cluster);
    }

    kprintf(" Done.\n");
    MemoryManager::Free(temp_buf, cluster_bytes);
    return entry.file_size;
}

void FAT32Driver::WriteFile(const char *name, const void *data, uint32_t size,
                            uint32_t parent_cluster)
{
    if (size == 0)
        return;

    kprintf("[FAT32] Writing file: %s (%d bytes)...\n", name, size);

    uint32_t cluster_size_bytes = sec_per_clus_ * 512;
    uint32_t bytes_remaining = size;
    uint32_t current_offset = 0;

    uint32_t first_cluster = 0;
    uint32_t prev_cluster = 0;
    uint32_t current_cluster = 0;

    // データをすべて書ききるまでループ
    while (bytes_remaining > 0)
    {
        // 1. 新しいクラスタを確保
        current_cluster = AllocateCluster();
        if (current_cluster == 0)
        {
            kprintf("[FAT32] Error: Disk Full!\n");
            return;
        }

        // 最初のクラスタなら記録しておく
        if (first_cluster == 0)
        {
            first_cluster = current_cluster;
        }
        else
        {
            // 2つ目以降なら、前のクラスタからこのクラスタへリンクを張る
            // (FATチェーン)
            LinkCluster(prev_cluster, current_cluster);
        }

        // 2. データを書き込む
        uint64_t target_lba = ClusterToLBA(current_cluster);

        // 今回書き込むサイズ (残り全部 or 1クラスタ分)
        uint32_t write_len = (bytes_remaining > cluster_size_bytes)
                                 ? cluster_size_bytes
                                 : bytes_remaining;

        // 書き込みデータ位置
        const uint8_t *src_ptr =
            static_cast<const uint8_t *>(data) + current_offset;

        // NVMeドライバはセクタ単位で書き込むため、端数がある場合はバッファリングが必要
        // しかしMemoryManagerのアライメント機能を使えば直接渡せる場合もある。
        // 今回は安全のため、必ずアライメントされた一時バッファを経由させる
        uint8_t *sector_buf = static_cast<uint8_t *>(
            MemoryManager::Allocate(cluster_size_bytes, 4096));
        memset(sector_buf, 0, cluster_size_bytes); // パディング部分は0埋め
        memcpy(sector_buf, src_ptr, write_len);

        // 1クラスタ分書き込み
        dev_->Write(target_lba, sector_buf, sec_per_clus_);
        MemoryManager::Free(sector_buf, cluster_size_bytes);

        // 3. 変数更新
        prev_cluster = current_cluster;
        bytes_remaining -= write_len;
        current_offset += write_len;
    }

    // 最後のクラスタは「終端(EOCC)」マーク
    // AllocateClusterで既に 0x0FFFFFFF が入っているはずだが、念の為
    LinkCluster(current_cluster, 0x0FFFFFFF);

    // 4. ディレクトリエントリ作成
    AddDirectoryEntry(name, first_cluster, size, 0x20, parent_cluster);

    kprintf("[FAT32] File Written Successfully (Start Cluster %d)\n",
            first_cluster);
}

void FAT32Driver::AppendFile(const char *name, const void *data, uint32_t size,
                             uint32_t parent_cluster)
{
    if (size == 0)
        return;

    // 1. ファイルが既に存在するか確認
    DirectoryEntry entry;
    uint32_t target_dir = (parent_cluster == 0) ? root_clus_ : parent_cluster;

    if (!FindDirectoryEntry(name, target_dir, &entry))
    {
        // ファイルが存在しない場合は新規作成
        WriteFile(name, data, size, parent_cluster);
        return;
    }

    kprintf("[FAT32] Appending to file: %s (%d bytes)...\n", name, size);

    // 2. 既存ファイルの最後のクラスタを探す
    uint32_t first_cluster = (entry.fst_clus_hi << 16) | entry.fst_clus_lo;
    uint32_t last_cluster = first_cluster;
    uint32_t old_size = entry.file_size;

    if (first_cluster == 0)
    {
        // ファイルサイズ0の場合、新しいクラスタを確保して開始
        first_cluster = AllocateCluster();
        if (first_cluster == 0)
        {
            kprintf("[FAT32] Error: Disk Full!\n");
            return;
        }
        last_cluster = first_cluster;
    }
    else
    {
        // 最後のクラスタまで辿る
        while (true)
        {
            uint32_t next = GetNextCluster(last_cluster);
            if (next >= 0x0FFFFFF8)
                break;
            last_cluster = next;
        }
    }

    // 3. 最後のクラスタの未使用領域を計算
    uint32_t cluster_size_bytes = sec_per_clus_ * 512;
    uint32_t used_in_last_cluster = old_size % cluster_size_bytes;
    if (used_in_last_cluster == 0 && old_size > 0)
        used_in_last_cluster = cluster_size_bytes; // 丁度埋まっている場合

    uint32_t free_in_last_cluster =
        (old_size == 0) ? cluster_size_bytes
                        : cluster_size_bytes - used_in_last_cluster;

    uint32_t bytes_remaining = size;
    uint32_t current_offset = 0;
    const uint8_t *src_ptr = static_cast<const uint8_t *>(data);

    // 4. 最後のクラスタの空き領域に書き込み
    if (free_in_last_cluster > 0 && old_size > 0)
    {
        uint64_t lba = ClusterToLBA(last_cluster);

        // 既存データを読み込む
        uint8_t *sector_buf = static_cast<uint8_t *>(
            MemoryManager::Allocate(cluster_size_bytes, 4096));
        dev_->Read(lba, sector_buf, sec_per_clus_);

        // 追記するサイズ
        uint32_t append_len = (bytes_remaining > free_in_last_cluster)
                                  ? free_in_last_cluster
                                  : bytes_remaining;

        // データを追記
        memcpy(sector_buf + used_in_last_cluster, src_ptr, append_len);

        // 書き戻す
        dev_->Write(lba, sector_buf, sec_per_clus_);
        MemoryManager::Free(sector_buf, cluster_size_bytes);

        bytes_remaining -= append_len;
        current_offset += append_len;
    }

    // 5. 残りのデータを新しいクラスタに書き込む
    uint32_t prev_cluster = last_cluster;

    while (bytes_remaining > 0)
    {
        // 新しいクラスタを確保
        uint32_t new_cluster = AllocateCluster();
        if (new_cluster == 0)
        {
            kprintf("[FAT32] Error: Disk Full!\n");
            break;
        }

        // 前のクラスタからリンク
        LinkCluster(prev_cluster, new_cluster);

        // データを書き込む
        uint64_t target_lba = ClusterToLBA(new_cluster);
        uint32_t write_len = (bytes_remaining > cluster_size_bytes)
                                 ? cluster_size_bytes
                                 : bytes_remaining;

        uint8_t *sector_buf = static_cast<uint8_t *>(
            MemoryManager::Allocate(cluster_size_bytes, 4096));
        memset(sector_buf, 0, cluster_size_bytes);
        memcpy(sector_buf, src_ptr + current_offset, write_len);

        dev_->Write(target_lba, sector_buf, sec_per_clus_);
        MemoryManager::Free(sector_buf, cluster_size_bytes);

        prev_cluster = new_cluster;
        bytes_remaining -= write_len;
        current_offset += write_len;
    }

    // 最後のクラスタに終端マーク
    LinkCluster(prev_cluster, 0x0FFFFFFF);

    // 6. ディレクトリエントリのファイルサイズと開始クラスタを更新
    uint32_t new_size = old_size + size;

    // ディレクトリエントリを探して更新
    uint32_t current_cluster = target_dir;
    uint8_t *buf = static_cast<uint8_t *>(
        MemoryManager::Allocate(sec_per_clus_ * 512, 4096));

    while (current_cluster < 0x0FFFFFF8)
    {
        uint64_t lba = ClusterToLBA(current_cluster);
        dev_->Read(lba, buf, sec_per_clus_);

        DirectoryEntry *entries = reinterpret_cast<DirectoryEntry *>(buf);
        int num_entries = (sec_per_clus_ * 512) / 32;

        for (int i = 0; i < num_entries; ++i)
        {
            if (entries[i].name[0] == 0x00)
                break;
            if ((unsigned char)entries[i].name[0] == 0xE5)
                continue;
            if (entries[i].attr == 0x0F)
                continue;

            if (IsNameEqual(entries[i].name, name))
            {
                // ファイルサイズを更新
                entries[i].file_size = new_size;

                // 開始クラスタも更新 (サイズ0だった場合)
                if (old_size == 0)
                {
                    entries[i].fst_clus_hi = (first_cluster >> 16) & 0xFFFF;
                    entries[i].fst_clus_lo = first_cluster & 0xFFFF;
                }

                // 該当セクタを書き戻す
                uint32_t sector_offset = i / 16;
                uint8_t *sector_ptr = buf + (sector_offset * 512);
                dev_->Write(lba + sector_offset, sector_ptr, 1);

                MemoryManager::Free(buf, sec_per_clus_ * 512);
                kprintf("[FAT32] File Appended Successfully (New Size: %d)\n",
                        new_size);
                return;
            }
        }

        current_cluster = GetNextCluster(current_cluster);
    }

    MemoryManager::Free(buf, sec_per_clus_ * 512);
    kprintf("[FAT32] Error: Could not update directory entry.\n");
}

void FAT32Driver::To83Format(const char *src, char *dst)
{
    memset(dst, ' ', 11);
    int src_idx = 0;
    int dst_idx = 0;

    while (src[src_idx] && src[src_idx] != '.' && dst_idx < 8)
    {
        char c = src[src_idx++];
        if (c >= 'a' && c <= 'z')
            c -= 32;
        dst[dst_idx++] = c;
    }

    while (src[src_idx] && src[src_idx] != '.')
        src_idx++;

    if (src[src_idx] == '.')
    {
        src_idx++;
        dst_idx = 8;
        while (src[src_idx] && dst_idx < 11)
        {
            char c = src[src_idx++];
            if (c >= 'a' && c <= 'z')
                c -= 32;
            dst[dst_idx++] = c;
        }
    }
}
} // namespace FileSystem