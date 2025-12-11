#pragma once
#include <stdint.h>

#include "block_device.hpp"
#include "fat32_defs.hpp"

namespace FileSystem
{

class FAT32Driver
{
  public:
    FAT32Driver(BlockDevice *dev, uint64_t partition_lba);

    void Initialize();

    uint32_t CreateDirectory(const char *name, uint32_t parent_cluster = 0);
    uint32_t EnsureDirectory(const char *path);
    void ListDirectory(uint32_t cluster = 0);
    uint32_t GetDirectoryCluster(const char *path, uint32_t base_cluster = 0);
    bool GetFileEntry(const char *path, DirectoryEntry *ret_entry,
                      uint32_t base_cluster = 0);
    uint32_t GetFileSize(const char *path);
    bool DeleteFile(const char *name, uint32_t parent_cluster = 0);
    uint32_t ReadFile(const char *name, void *buffer, uint32_t buffer_size,
                      uint32_t base_cluster = 0);
    void WriteFile(const char *name, const void *data, uint32_t size,
                   uint32_t parent_cluster = 0);
    void AppendFile(const char *name, const void *data, uint32_t size,
                    uint32_t parent_cluster = 0);

    static void To83Format(const char *src, char *dst);

    // 別のFAT32ファイルシステムからファイルをコピー
    // src_fs: コピー元ファイルシステム
    // src_path: コピー元パス
    // dst_path: コピー先パス
    // 戻り値: 成功時true
    bool CopyFileFrom(FAT32Driver *src_fs, const char *src_path,
                      const char *dst_path);

  private:
    BlockDevice *dev_;

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
    void AddDirectoryEntry(const char *name, uint32_t start_cluster,
                           uint32_t size, uint8_t attr,
                           uint32_t parent_cluster);
    // ファイル名からディレクトリエントリを探す内部関数
    // name: 探したいファイル名
    // parent_cluster: 検索対象のディレクトリ (0=ルート)
    // found_entry: 見つかったエントリのコピーを格納する先
    // return: 見つかったらtrue
    bool FindDirectoryEntry(const char *name, uint32_t parent_cluster,
                            DirectoryEntry *found_entry);
};

extern FAT32Driver *g_fat32_driver;
extern FAT32Driver *g_system_fs;
} // namespace FileSystem