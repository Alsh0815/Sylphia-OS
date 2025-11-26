#pragma once
#include <stdint.h>

namespace FileSystem
{
    // 指定されたセクタ数をFAT32でフォーマットする
    void FormatPartitionFAT32(uint64_t part_sectors);
}