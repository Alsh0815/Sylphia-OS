#pragma once
#include <stdint.h>

namespace FileSystem
{
class FAT32Driver; // 前方宣言

// NVMeディスク全体を初期化し、単一のGPTパーティションを作成する
// total_blocks: ディスクの総セクタ数 (Identify Namespaceで取得したnsze)
void FormatDiskGPT(uint64_t total_blocks);

// パーティションをFAT32でフォーマットする
void FormatPartitionFAT32(uint64_t partition_blocks);

// システムファイルのインストールを実行
// nvme_fs: NVMeファイルシステム（コピー先）
// already_installed: 既にインストール済みかどうか
void RunInstaller(FAT32Driver *nvme_fs, bool already_installed);
} // namespace FileSystem
