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
        // ディレクトリ内のファイルを表示する
        // cluster: 表示したいディレクトリのクラスタ (0=ルート)
        void ListDirectory(uint32_t cluster = 0);
        // ファイルを削除する
        // name: ファイル名
        // parent_cluster: 親ディレクトリ (0=ルート)
        // return: 成功したらtrue
        bool DeleteFile(const char *name, uint32_t parent_cluster = 0);
        // ファイルを読み込む
        // name: ファイル名 (8.3形式, 例: "KERNEL  ELF")
        // buffer: 読み込み先のバッファ
        // buffer_size: バッファのサイズ
        // return: 実際に読み込んだサイズ
        uint32_t ReadFile(const char *name, void *buffer, uint32_t buffer_size);
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
        uint32_t AllocateCluster(); // 空きクラスタを1つ確保して返す
        // 指定したクラスタの次のクラスタ番号をFATから読み取る
        uint32_t GetNextCluster(uint32_t current_cluster);
        void LinkCluster(uint32_t current, uint32_t next); // FATテーブルを更新
        // 指定したクラスタから始まるFATチェーンを全て解放(0)にする ■■■
        void FreeChain(uint32_t start_cluster);
        // 8.3形式のファイル名比較ヘルパー
        // entry_name: ディレクトリエントリ内の名前 (スペース埋めあり)
        // target_name: 比較したい名前 (ドットあり)
        bool IsNameEqual(const char *entry_name, const char *target_name);
        // ディレクトリエントリを追加する
        // name: ファイル名 (8.3形式, 例: "KERNEL  ELF")
        // start_cluster: ファイルの開始クラスタ
        // size: ファイルサイズ
        // attr: ファイル属性 (例: 0x20=ファイル, 0x10=ディレクトリ)
        // parent_cluster: 親ディレクトリのクラスタ番号 (0=ルートディレクトリ)
        void AddDirectoryEntry(const char *name, uint32_t start_cluster, uint32_t size, uint8_t attr, uint32_t parent_cluster);
        // ファイル名からディレクトリエントリを探す内部関数
        // name: 探したいファイル名
        // parent_cluster: 検索対象のディレクトリ (0=ルート)
        // found_entry: 見つかったエントリのコピーを格納する先
        // return: 見つかったらtrue
        bool FindDirectoryEntry(const char *name, uint32_t parent_cluster, DirectoryEntry *found_entry);
        // パスからディレクトリエントリを取得する内部関数
        // ret_entry: 見つかったエントリの格納先
        bool GetFileEntry(const char *path, DirectoryEntry *ret_entry);
    };

    extern FAT32Driver *g_fat32_driver; // グローバルインスタンス
}