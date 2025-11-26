#pragma once
#include <stdint.h>

#include "fat32_defs.hpp"

namespace FileSystem
{

    class FAT32Driver
    {
    public:
        // コンストラクタ: パーティションの開始LBAを受け取る
        FAT32Driver(uint64_t partition_lba);

        // 初期化 (BPBを読み込んでパラメータを計算)
        void Initialize();

        // ディレクトリを作成する
        uint32_t CreateDirectory(const char *name, uint32_t parent_cluster = 0);
        // ファイルをルートディレクトリに保存する
        // name: ファイル名 (8.3形式, 例: "KERNEL  ELF")
        // data: データの中身
        // size: データサイズ
        // parent_cluster: 親ディレクトリのクラスタ番号 (省略時はルートディレクトリ)
        void WriteFile(const char *name, const void *data, uint32_t size, uint32_t parent_cluster = 0);

    private:
        uint64_t part_lba_;
        uint32_t sec_per_clus_;
        uint32_t reserved_sectors_;
        uint32_t num_fats_;
        uint32_t fat_sz32_;
        uint32_t root_clus_;

        // 計算済みオフセット (LBA)
        uint64_t fat_start_lba_;
        uint64_t data_start_lba_;

        // ヘルパー関数
        uint64_t ClusterToLBA(uint32_t cluster);
        uint32_t AllocateCluster();                        // 空きクラスタを1つ確保して返す
        void LinkCluster(uint32_t current, uint32_t next); // FATテーブルを更新
        void AddDirectoryEntry(const char *name, uint32_t start_cluster, uint32_t size, uint8_t attr, uint32_t parent_cluster);
    };
}