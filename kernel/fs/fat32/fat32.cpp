#include "driver/nvme/nvme_driver.hpp"
#include "fs/fat32/fat32_defs.hpp"
#include "memory/memory_manager.hpp"
#include "cxx.hpp"
#include "printk.hpp"

namespace FileSystem
{

    // パーティション1の開始LBA (GPT作成時に2048に設定)
    const uint64_t kPartitionStartLBA = 2048;

    void FormatPartitionFAT32(uint64_t part_sectors)
    {
        kprintf("[Installer] Formatting Partition 1 as FAT32...\n");

        // バッファ確保
        uint8_t *buf = static_cast<uint8_t *>(MemoryManager::Allocate(512, 4096));
        memset(buf, 0, 512);

        // -----------------------------------------
        // パラメータ計算
        // -----------------------------------------
        // クラスタサイズ: 1GBなら 4KB (8セクタ) が一般的
        uint8_t sec_per_clus = 8;
        uint16_t reserved_sectors = 32;
        uint8_t num_fats = 2;

        // FAT領域のサイズ計算
        // おおよそ (TotalSectors / SecPerClus) * 4バイト がFATに必要なサイズ
        // これを512バイトで割ってセクタ数を出す
        uint32_t total_clusters = part_sectors / sec_per_clus;
        uint32_t fat_sz_bytes = total_clusters * 4;
        uint32_t fat_sz_sec = (fat_sz_bytes + 511) / 512;

        kprintf("[Format] Total Sectors: %lld, FAT Size: %d sectors\n", part_sectors, fat_sz_sec);

        // -----------------------------------------
        // 1. Boot Sector (BPB) 作成 & 書き込み
        // -----------------------------------------
        FAT32_BPB *bpb = reinterpret_cast<FAT32_BPB *>(buf);

        // Jump Code (x86 JMP)
        bpb->jmp_boot[0] = 0xEB;
        bpb->jmp_boot[1] = 0x58;
        bpb->jmp_boot[2] = 0x90;
        memcpy(bpb->oem_name, "MSWIN4.1", 8);

        bpb->bytes_per_sec = 512;
        bpb->sec_per_clus = sec_per_clus;
        bpb->reserved_sec_cnt = reserved_sectors;
        bpb->num_fats = num_fats;
        bpb->root_ent_cnt = 0;
        bpb->tot_sec16 = 0;
        bpb->media = 0xF8;
        bpb->fat_sz16 = 0;
        bpb->sec_per_trk = 32;
        bpb->num_heads = 64;
        bpb->hidd_sec = kPartitionStartLBA;
        bpb->tot_sec32 = (uint32_t)part_sectors;

        bpb->fat_sz32 = fat_sz_sec;
        bpb->ext_flags = 0;
        bpb->fs_ver = 0;
        bpb->root_clus = 2;   // Root Directoryはクラスタ2から
        bpb->fs_info = 1;     // FSInfoはセクタ1
        bpb->bk_boot_sec = 6; // バックアップはセクタ6

        bpb->drv_num = 0x80;
        bpb->boot_sig = 0x29;
        bpb->vol_id = 0x12345678;
        memcpy(bpb->vol_lab, "SYLPHIA OS ", 11);
        memcpy(bpb->fil_sys_type, "FAT32   ", 8);
        bpb->signature = 0xAA55;

        NVMe::g_nvme->Write(kPartitionStartLBA + 0, buf, 1);
        // バックアップBPB (セクタ6)
        NVMe::g_nvme->Write(kPartitionStartLBA + 6, buf, 1);

        kprintf("[Format] BPB Written.\n");

        // -----------------------------------------
        // 2. FSInfo Sector 作成 & 書き込み
        // -----------------------------------------
        memset(buf, 0, 512);
        FAT32_FSInfo *fsinfo = reinterpret_cast<FAT32_FSInfo *>(buf);

        fsinfo->lead_sig = 0x41615252;
        fsinfo->struc_sig = 0x61417272;
        fsinfo->free_count = total_clusters - 1; // クラスタ2(Root)以外空き
        fsinfo->nxt_free = 3;                    // 次はクラスタ3から使える
        fsinfo->trail_sig = 0xAA550000;

        NVMe::g_nvme->Write(kPartitionStartLBA + 1, buf, 1);
        // バックアップFSInfo (セクタ7)
        NVMe::g_nvme->Write(kPartitionStartLBA + 7, buf, 1);

        kprintf("[Format] FSInfo Written.\n");

        // -----------------------------------------
        // 3. FAT初期化 (FAT1 & FAT2)
        // -----------------------------------------
        // FATの先頭2エントリ(クラスタ0, 1)は予約済み
        // クラスタ2(Root)は終端マーク(EOCC)
        memset(buf, 0, 512);
        uint32_t *fat_entries = reinterpret_cast<uint32_t *>(buf);

        fat_entries[0] = 0x0FFFFFF8; // Media Type
        fat_entries[1] = 0x0FFFFFFF; // Clean Shutdown / Hard Error
        fat_entries[2] = 0x0FFFFFFF; // Root Directory (End of Chain)

        // FAT1の先頭セクタ
        uint64_t fat1_start = kPartitionStartLBA + reserved_sectors;
        NVMe::g_nvme->Write(fat1_start, buf, 1);

        // FAT2の先頭セクタ
        uint64_t fat2_start = fat1_start + fat_sz_sec;
        NVMe::g_nvme->Write(fat2_start, buf, 1);

        memset(buf, 0, 512);
        // FAT1の残り (セクタ1から127まで)
        for (uint32_t i = 1; i < 128; ++i)
        {
            NVMe::g_nvme->Write(fat1_start + i, buf, 1);
        }
        // FAT2の残り
        for (uint32_t i = 1; i < 128; ++i)
        {
            NVMe::g_nvme->Write(fat2_start + i, buf, 1);
        }

        kprintf("[Format] FAT Tables Initialized (Partial Clear).\n");

        // -----------------------------------------
        // 4. Root Directory 初期化 (Cluster 2)
        // -----------------------------------------
        // ルートディレクトリの実体がある場所
        // Data Start = Reserved + (NumFATs * FATSz)
        uint64_t data_start_lba = kPartitionStartLBA + reserved_sectors + (num_fats * fat_sz_sec);
        // クラスタ2のLBA = Data Start + ((2 - 2) * SecPerClus) = Data Start

        // 完全に空にする
        memset(buf, 0, 512);

        // 1クラスタ分(8セクタ)を0クリア
        for (int i = 0; i < sec_per_clus; ++i)
        {
            NVMe::g_nvme->Write(data_start_lba + i, buf, 1);
        }

        kprintf("[Format] Root Directory Initialized.\n");
        kprintf("[Installer] FAT32 Format Complete!\n");

        MemoryManager::Free(buf, 512);
    }
}