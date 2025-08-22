#pragma once
#include <cstddef>
#include <cstdint>
#include "fs_types.hpp"

class Console;
class BlockDevice;

// 各 FS ドライバが実装するインタフェース
struct FsDriver
{
    virtual ~FsDriver() = default;

    // ドライバ名（静的文字列でOK）
    virtual const char *name() const = 0;

    // 対象 BlockDevice が自分の FS かどうかを判定（true=たぶん自分）
    // device は「生デバイス」または「パーティションスライス」（4KiB view）
    virtual bool probe(BlockDevice &device, Console &con) = 0;

    // mount 実体（成功時 FsMount* を返す）。失敗なら nullptr。
    virtual FsMount *mount(BlockDevice &device, Console &con) = 0;
};

// VFS 登録/マウント API
namespace vfs
{

    // ドライバ登録（最大 8 個）
    bool register_driver(FsDriver *drv);

    // 自動判定でマウント（パーティション指定なし＝デバイス全域を試す）
    FsStatus mount_auto(BlockDevice &device, Console &con, FsMount **out);

    // パーティション配列を試す（先頭から順に probe → mount）
    FsStatus mount_auto_on_partitions(BlockDevice &device,
                                      const PartitionInfo *parts, size_t num_parts,
                                      Console &con, FsMount **out);

    // ドライバ一覧の取得（デバッグ用）
    size_t enumerate_drivers(const FsDriver **out_array, size_t max);

    // 解除（FsMount の unmount を呼ぶだけの小ヘルパ）
    inline void unmount(FsMount *mnt, Console &con)
    {
        if (mnt)
            mnt->unmount(con);
    }

    bool mkdir(FsMount *mnt, const char *path, Console &con);
    bool create(FsMount *mnt, const char *path, Console &con);
    bool write(FsMount *mnt, const char *path, const void *buf, uint64_t len, uint64_t off, Console &con);
    bool read(FsMount *mnt, const char *path, void *buf, uint64_t len, uint64_t off, Console &con);
    bool stat(FsMount *mnt, const char *path, VfsStat &st, Console &con);

} // namespace vfs
