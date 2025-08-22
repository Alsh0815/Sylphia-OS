#pragma once
#include <cstdint>

class Console;
class BlockDevice;

// VFS 共通の戻り値
enum class FsStatus : int32_t
{
    Ok = 0,
    NotSupported = -1,
    ProbeFailed = -2,
    MountFailed = -3,
    InvalidArg = -4,
    IoError = -5,
};

// パーティション情報（4KiB 論理ブロック単位）
struct PartitionInfo
{
    uint64_t first_lba4k; // 開始 4KiB ブロック
    uint64_t blocks4k;    // 長さ（4KiB ブロック数）
    // 将来: GPTのtype GUIDや名前などを拡張
};

// マウントハンドル（FS 実装が継承）
struct FsMount
{
    virtual ~FsMount() = default;
    virtual void unmount(Console &con) = 0;
};
