#pragma once
#include <cstddef>
#include <cstdint>
#include "../fs/fs_types.hpp"

class Console;
class BlockDevice;

namespace gpt
{

    // GUID 型（UEFI/GPT）
    struct Guid
    {
        uint32_t a;
        uint16_t b;
        uint16_t c;
        uint8_t d[8];
    } __attribute__((packed));

    static_assert(sizeof(Guid) == 16, "Guid must be 16 bytes");

    // スキャン結果のメタ（必要最低限）
    struct GptMeta
    {
        uint64_t header_lba;  // 一次ヘッダ LBA（物理LBA換算）
        uint64_t entries_lba; // エントリ開始 LBA（物理LBA）
        uint32_t entry_size;  // 通常 128
        uint32_t entry_count; // エントリ総数
    };

    // GPT スキャン（成功時 true）
    // - out_parts: 見つかったパーティションを 4KiB 単位で格納
    // - max_parts: out_parts の要素数
    // - out_found: 実際に格納した件数（nullptr 可）
    // - out_meta  : GPT ヘッダ情報（nullptr 可）
    // 注意: CRC/整合性が NG の場合は false を返します。
    bool scan(BlockDevice &dev,
              PartitionInfo *out_parts, size_t max_parts,
              size_t *out_found,
              GptMeta *out_meta,
              Console &con);

    // GUID 比較（ユーティリティ）
    bool is_zero_guid(const Guid &g);

} // namespace gpt
