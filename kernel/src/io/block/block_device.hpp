#pragma once
#include <cstddef>
#include <cstdint>

class Console;

// 論理 4KiB ブロックを基本単位に扱う Block I/O 抽象。
// 物理デバイスのセクタサイズ（例: 512B）は実装側で吸収する。
class BlockDevice
{
public:
    enum WriteVerifyMode : uint32_t
    {
        kNoVerify = 0,
        kVerifyAfterWrite = 1, // 書き込み直後に 1 ブロック読み戻して比較（重要パス向け）
    };

    virtual ~BlockDevice() = default;

    // 4KiB 論理ブロック数（未知なら 0）
    virtual uint64_t logical_block_count_4k() const = 0;

    // デバイスの物理セクタバイト数（例: 512 / 4096）
    virtual uint32_t physical_sector_bytes() const = 0;

    // 4KiB 論理ブロック読み取り
    // lba4k: 4KiB単位の先頭ブロック番号, count: 4KiBブロック数
    virtual bool read_blocks_4k(uint64_t lba4k, uint32_t count, void *buf, size_t buf_bytes, Console &con) = 0;

    // 4KiB 論理ブロック書き込み（必要に応じて FUA/Verify を使用）
    virtual bool write_blocks_4k(uint64_t lba4k, uint32_t count, const void *buf, size_t buf_bytes,
                                 bool fua, WriteVerifyMode verify, Console &con) = 0;

    // 必要なら明示フラッシュ
    virtual bool flush(Console &con) = 0;
};
