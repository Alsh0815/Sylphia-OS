#include "driver/nvme/nvme_driver.hpp"
#include "memory/memory_manager.hpp"
#include "cxx.hpp"
#include "printk.hpp"

#include "fat32_driver.hpp"

namespace FileSystem
{

    FAT32Driver::FAT32Driver(uint64_t partition_lba)
        : part_lba_(partition_lba) {}

    void FAT32Driver::Initialize()
    {
        // BPB (LBA 0) を読み込む
        uint8_t *buf = static_cast<uint8_t *>(MemoryManager::Allocate(512, 4096));
        NVMe::g_nvme->Read(part_lba_, buf, 1);

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

        kprintf("[FAT32] Driver Initialized. ClusterSize=%d sectors\n", sec_per_clus_);
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
        NVMe::g_nvme->Read(fat_start_lba_, buf, 1);

        for (int i = 2; i < 128; ++i)
        { // 1セクタには128個のエントリ (512/4)
            if (entries[i] == 0)
            {
                // 空き発見！使用中(EOCC = 0x0FFFFFFF)マークをつけて保存
                entries[i] = 0x0FFFFFFF;
                NVMe::g_nvme->Write(fat_start_lba_, buf, 1);
                // FAT2(バックアップ)も更新
                NVMe::g_nvme->Write(fat_start_lba_ + fat_sz32_, buf, 1);

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
        NVMe::g_nvme->Read(target_lba, buf, 1);

        uint32_t *entries = reinterpret_cast<uint32_t *>(buf);
        entries[entry_offset] = next; // リンク更新

        // 書き戻す
        NVMe::g_nvme->Write(target_lba, buf, 1);

        // バックアップFAT(FAT2)も更新
        NVMe::g_nvme->Write(target_lba + fat_sz32_, buf, 1);

        MemoryManager::Free(buf, 512);
    }

    void FAT32Driver::AddDirectoryEntry(const char *name, uint32_t start_cluster, uint32_t size, uint8_t attr, uint32_t parent_cluster)
    {
        // 親ディレクトリの開始クラスタを決定 (0ならルート)
        uint32_t target_cluster = (parent_cluster == 0) ? root_clus_ : parent_cluster;
        uint64_t lba = ClusterToLBA(target_cluster);

        uint8_t *buf = static_cast<uint8_t *>(MemoryManager::Allocate(512, 4096));

        // 親ディレクトリの中身を走査 (簡易的に先頭クラスタのみ探索)
        for (int s = 0; s < sec_per_clus_; ++s)
        {
            NVMe::g_nvme->Read(lba + s, buf, 1);
            DirectoryEntry *dir = reinterpret_cast<DirectoryEntry *>(buf);

            for (int i = 0; i < 16; ++i)
            {
                if (dir[i].name[0] == 0x00 || (unsigned char)dir[i].name[0] == 0xE5)
                {
                    // エントリ作成
                    memset(&dir[i], 0, 32);
                    memcpy(dir[i].name, name, 11);
                    dir[i].attr = attr; // [修正] 引数の属性を使う (ファイル:0x20, ディレクトリ:0x10)
                    dir[i].fst_clus_hi = (start_cluster >> 16) & 0xFFFF;
                    dir[i].fst_clus_lo = start_cluster & 0xFFFF;
                    dir[i].file_size = size;

                    NVMe::g_nvme->Write(lba + s, buf, 1);
                    MemoryManager::Free(buf, 512);
                    return;
                }
            }
        }
        MemoryManager::Free(buf, 512);
        kprintf("[FAT32] Directory full!\n");
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
        uint8_t *buf = static_cast<uint8_t *>(MemoryManager::Allocate(cluster_bytes, 4096));
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

        NVMe::g_nvme->Write(target_lba, buf, sec_per_clus_);
        MemoryManager::Free(buf, cluster_bytes);

        // 3. FATチェーン終端
        LinkCluster(new_cluster, 0x0FFFFFFF);

        // 4. 親ディレクトリにこのディレクトリのエントリを追加
        // 属性 0x10 (Directory), サイズ 0
        AddDirectoryEntry(name, new_cluster, 0, 0x10, parent_cluster);

        return new_cluster;
    }

    void FAT32Driver::WriteFile(const char *name, const void *data, uint32_t size, uint32_t parent_cluster)
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
                // 2つ目以降なら、前のクラスタからこのクラスタへリンクを張る (FATチェーン)
                LinkCluster(prev_cluster, current_cluster);
            }

            // 2. データを書き込む
            uint64_t target_lba = ClusterToLBA(current_cluster);

            // 今回書き込むサイズ (残り全部 or 1クラスタ分)
            uint32_t write_len = (bytes_remaining > cluster_size_bytes) ? cluster_size_bytes : bytes_remaining;

            // 書き込みデータ位置
            const uint8_t *src_ptr = static_cast<const uint8_t *>(data) + current_offset;

            // NVMeドライバはセクタ単位で書き込むため、端数がある場合はバッファリングが必要
            // しかしMemoryManagerのアライメント機能を使えば直接渡せる場合もある。
            // 今回は安全のため、必ずアライメントされた一時バッファを経由させる
            uint8_t *sector_buf = static_cast<uint8_t *>(MemoryManager::Allocate(cluster_size_bytes, 4096));
            memset(sector_buf, 0, cluster_size_bytes); // パディング部分は0埋め
            memcpy(sector_buf, src_ptr, write_len);

            // 1クラスタ分書き込み
            NVMe::g_nvme->Write(target_lba, sector_buf, sec_per_clus_);
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

        kprintf("[FAT32] File Written Successfully (Start Cluster %d)\n", first_cluster);
    }
}