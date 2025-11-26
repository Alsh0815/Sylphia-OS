#pragma once
#include <stdint.h>

namespace FileSystem
{

    // GUID (16 bytes)
    struct GUID
    {
        uint32_t data1;
        uint16_t data2;
        uint16_t data3;
        uint8_t data4[8];
    } __attribute__((packed));

    // GPT Header (LBA 1)
    struct GPTHeader
    {
        uint64_t signature;                   // "EFI PART" (0x5452415020494645)
        uint32_t revision;                    // 0x00010000 (1.0)
        uint32_t header_size;                 // 92 bytes
        uint32_t crc32;                       // Header CRC32 (計算時は0にする)
        uint32_t reserved1;                   // 0
        uint64_t my_lba;                      // このヘッダーがあるLBA (通常1)
        uint64_t alternate_lba;               // バックアップヘッダーのLBA (ディスク末尾)
        uint64_t first_usable_lba;            // パーティション開始可能LBA (通常34)
        uint64_t last_usable_lba;             // パーティション終了可能LBA
        GUID disk_guid;                       // ディスク自体のGUID
        uint64_t partition_entry_lba;         // エントリ配列の開始LBA (通常2)
        uint32_t num_partition_entries;       // エントリ数 (通常128)
        uint32_t sizeof_partition_entry;      // エントリサイズ (通常128)
        uint32_t partition_entry_array_crc32; // エントリ配列のCRC32
        uint8_t reserved2[420];               // 512バイトまでのパディング
    } __attribute__((packed));

    // GPT Partition Entry (LBA 2~)
    struct GPTPartitionEntry
    {
        GUID type_guid;      // パーティションタイプGUID
        GUID unique_guid;    // パーティション固有GUID
        uint64_t first_lba;  // 開始LBA
        uint64_t last_lba;   // 終了LBA
        uint64_t attributes; // 属性フラグ
        char16_t name[36];   // パーティション名 (UTF-16LE)
    } __attribute__((packed));

    // Protective MBR (LBA 0)
    // 古いツールがディスクを「空き」と誤認しないためのダミー
    struct LegacyMBR
    {
        uint8_t bootstrap[446];
        struct
        {
            uint8_t boot_indicator;
            uint8_t start_head;
            uint8_t start_sector;
            uint8_t start_cyl;
            uint8_t sys_type; // 0xEE = GPT Protective
            uint8_t end_head;
            uint8_t end_sector;
            uint8_t end_cyl;
            uint32_t start_lba;
            uint32_t size_lba;
        } partitions[4];
        uint16_t signature; // 0xAA55
    } __attribute__((packed));

}