#pragma once
#include <stdint.h>

namespace FileSystem
{
    // NVMeディスク全体を初期化し、単一のGPTパーティションを作成する
    // total_blocks: ディスクの総セクタ数 (Identify Namespaceで取得したnsze)
    void FormatDiskGPT(uint64_t total_blocks);
}