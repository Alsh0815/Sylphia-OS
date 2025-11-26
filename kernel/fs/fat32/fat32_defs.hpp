#pragma once
#include <stdint.h>

namespace FileSystem
{

    // FAT32 Boot Sector (BPB)
    // LBA 0 (パーティション内相対)
    struct FAT32_BPB
    {
        uint8_t jmp_boot[3];       // Jump Code (EB 58 90 etc)
        char oem_name[8];          // "MSWIN4.1" etc
        uint16_t bytes_per_sec;    // 512
        uint8_t sec_per_clus;      // 1, 8, etc.
        uint16_t reserved_sec_cnt; // 32 etc.
        uint8_t num_fats;          // 2
        uint16_t root_ent_cnt;     // 0 for FAT32
        uint16_t tot_sec16;        // 0 for FAT32
        uint8_t media;             // 0xF8 (Fixed Disk)
        uint16_t fat_sz16;         // 0 for FAT32
        uint16_t sec_per_trk;
        uint16_t num_heads;
        uint32_t hidd_sec;  // Partition LBA Start
        uint32_t tot_sec32; // Total Sectors

        // FAT32 Structure
        uint32_t fat_sz32; // Sectors per FAT
        uint16_t ext_flags;
        uint16_t fs_ver;      // 0
        uint32_t root_clus;   // 通常 2
        uint16_t fs_info;     // 通常 1 (FSInfo Sector LBA)
        uint16_t bk_boot_sec; // 通常 6
        uint8_t reserved[12];
        uint8_t drv_num;
        uint8_t reserved1;
        uint8_t boot_sig; // 0x29
        uint32_t vol_id;
        char vol_lab[11];     // "SYLPHIA OS "
        char fil_sys_type[8]; // "FAT32   "
        uint8_t code[420];    // Boot Code (Dummy)
        uint16_t signature;   // 0xAA55
    } __attribute__((packed));

    // FSInfo Sector
    // LBA 1 (パーティション内相対)
    struct FAT32_FSInfo
    {
        uint32_t lead_sig; // 0x41615252 ("RRaA")
        uint8_t reserved1[480];
        uint32_t struc_sig;  // 0x61417272 ("rrAa")
        uint32_t free_count; // 空きクラスタ数 (計算前は 0xFFFFFFFF)
        uint32_t nxt_free;   // 次の空きクラスタ (通常 2)
        uint8_t reserved2[12];
        uint32_t trail_sig; // 0xAA550000
    } __attribute__((packed));

    // ディレクトリエントリ (32 bytes)
    struct DirectoryEntry
    {
        char name[11]; // 8.3形式
        uint8_t attr;
        uint8_t nt_res;
        uint8_t crt_time_tenth;
        uint16_t crt_time;
        uint16_t crt_date;
        uint16_t lst_acc_date;
        uint16_t fst_clus_hi;
        uint16_t wrt_time;
        uint16_t wrt_date;
        uint16_t fst_clus_lo;
        uint32_t file_size;
    } __attribute__((packed));

}