#include "driver/nvme/nvme_driver.hpp"
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
        uint8_t *buf = new uint8_t[512];
        NVMe::g_nvme->Read(part_lba_, buf, 1);

        FAT32_BPB *bpb = reinterpret_cast<FAT32_BPB *>(buf);

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
        delete[] buf;
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

        uint8_t *buf = new uint8_t[512];
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

                delete[] buf;
                return i;
            }
        }

        delete[] buf;
        kprintf("[FAT32] No free clusters found in first FAT sector!\n");
        return 0; // Error
    }

    void FAT32Driver::AddDirectoryEntry(const char *name, uint32_t start_cluster, uint32_t size)
    {
        // ルートディレクトリ(Cluster 2)を読み込んで空きエントリを探す
        uint64_t lba = ClusterToLBA(root_clus_);
        uint8_t *buf = new uint8_t[512];

        // 1クラスタ分(8セクタ)走査
        for (int s = 0; s < sec_per_clus_; ++s)
        {
            NVMe::g_nvme->Read(lba + s, buf, 1);
            DirectoryEntry *dir = reinterpret_cast<DirectoryEntry *>(buf);

            for (int i = 0; i < 16; ++i)
            { // 1セクタに16エントリ (512/32)
                // 0x00(未割り当て) または 0xE5(削除済み) なら使える
                if (dir[i].name[0] == 0x00 || (unsigned char)dir[i].name[0] == 0xE5)
                {
                    // エントリ作成
                    memset(&dir[i], 0, 32);
                    memcpy(dir[i].name, name, 11);
                    dir[i].attr = 0x20; // Archive
                    dir[i].fst_clus_hi = (start_cluster >> 16) & 0xFFFF;
                    dir[i].fst_clus_lo = start_cluster & 0xFFFF;
                    dir[i].file_size = size;

                    // 書き戻し
                    NVMe::g_nvme->Write(lba + s, buf, 1);
                    delete[] buf;
                    return;
                }
            }
        }
        delete[] buf;
        kprintf("[FAT32] Root directory full!\n");
    }

    void FAT32Driver::WriteFile(const char *name, const void *data, uint32_t size)
    {
        if (size == 0)
            return;

        kprintf("[FAT32] Writing file: %s (%d bytes)...\n", name, size);

        // 必要なクラスタ数を計算
        uint32_t cluster_size_bytes = sec_per_clus_ * 512;

        // 今回は簡易実装のため「1クラスタ(4KB)に収まるファイル」のみ対応
        // 複数クラスタ対応（FATチェーン）は次のステップで
        if (size > cluster_size_bytes)
        {
            kprintf("[FAT32] Error: File too large for single cluster demo.\n");
            return;
        }

        // 空きクラスタ確保
        uint32_t cluster = AllocateCluster();
        if (cluster == 0)
            return;

        // データをクラスタに書き込む
        uint64_t target_lba = ClusterToLBA(cluster);

        // 1セクタ未満ならパディングが必要だが、
        // NVMeドライバのWriteは指定セクタ数分書くので、
        // バッファをセクタ境界に合わせて用意する
        uint8_t *sector_buf = new uint8_t[cluster_size_bytes];
        memset(sector_buf, 0, cluster_size_bytes);
        memcpy(sector_buf, data, size);

        // 書き込み (1クラスタ分 = sec_per_clus_ セクタ)
        NVMe::g_nvme->Write(target_lba, sector_buf, sec_per_clus_);
        delete[] sector_buf;

        // ディレクトリエントリ作成
        AddDirectoryEntry(name, cluster, size);

        kprintf("[FAT32] File Written Successfully to Cluster %d\n", cluster);
    }
}