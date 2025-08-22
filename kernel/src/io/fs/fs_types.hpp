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

enum class VfsFileType : uint16_t
{
    kUnknown = 0,
    kFile = 1,
    kDirectory = 2,
    // 将来的にシンボリックリンクなども追加可能
};

struct VfsStat
{
    VfsFileType type;  // ファイル種別
    uint16_t mode;     // パーミッション (例: 0755)
    uint32_t links;    // ハードリンク数
    uint64_t size;     // ファイルサイズ (バイト)
    uint64_t inode_id; // inode番号 (FSがサポートする場合)

    // 各種タイムスタンプ (FSがサポートしない場合は0)
    uint64_t atime; // 最終アクセス時刻
    uint64_t mtime; // 最終更新時刻
    uint64_t ctime; // 作成時刻 or Inode変更時刻
};

// マウントハンドル（FS 実装が継承）
struct FsMount
{
    virtual ~FsMount() = default;
    virtual void unmount(Console &con) = 0;

    virtual bool mkdir_path(const char *abs_path, Console &con) = 0;
    virtual bool create_path(const char *abs_path, Console &con) = 0;
    virtual bool write_path(const char *abs_path, const void *buf, uint64_t len, uint64_t off, Console &con) = 0;
    virtual bool read_path(const char *abs_path, void *buf, uint64_t len, uint64_t off, Console &con) = 0;
    virtual bool stat_path(const char *abs_path, VfsStat &st, Console &con) = 0;
    virtual bool unlink_path(const char *abs_path, Console &con) = 0;
    virtual bool rmdir_path(const char *abs_path, Console &con) = 0;
    virtual bool truncate_path(const char *abs_path, uint64_t new_size, Console &con) = 0;
    virtual bool rename_path(const char *old_path, const char *new_path, Console &con) = 0;
};
