#include "vfs.hpp"
#include "../block/block_slice.hpp"
#include "../../console.hpp"

// 登録テーブル（固定長）
static FsDriver *g_drivers[8];
static size_t g_driver_count = 0;

namespace vfs
{

    bool register_driver(FsDriver *drv)
    {
        if (!drv)
            return false;
        if (g_driver_count >= sizeof(g_drivers) / sizeof(g_drivers[0]))
            return false;
        g_drivers[g_driver_count++] = drv;
        return true;
    }

    size_t enumerate_drivers(const FsDriver **out_array, size_t max)
    {
        size_t n = (g_driver_count < max) ? g_driver_count : max;
        for (size_t i = 0; i < n; ++i)
            out_array[i] = g_drivers[i];
        return n;
    }

    static FsStatus try_mount_on(BlockDevice &view, Console &con, FsMount **out)
    {
        for (size_t i = 0; i < g_driver_count; ++i)
        {
            FsDriver *d = g_drivers[i];
            if (!d)
                continue;
            con.printf("VFS: probing with driver: %s\n", d->name());
            if (!d->probe(view, con))
                continue;
            con.printf("VFS: %s: probe OK, trying mount...\n", d->name());
            FsMount *m = d->mount(view, con);
            if (m)
            {
                *out = m;
                return FsStatus::Ok;
            }
            con.printf("VFS: %s: mount failed\n", d->name());
        }
        return (g_driver_count == 0) ? FsStatus::NotSupported : FsStatus::ProbeFailed;
    }

    FsStatus mount_auto(BlockDevice &device, Console &con, FsMount **out)
    {
        if (!out)
            return FsStatus::InvalidArg;
        *out = nullptr;
        return try_mount_on(device, con, out);
    }

    FsStatus mount_auto_on_partitions(BlockDevice &device,
                                      const PartitionInfo *parts, size_t num_parts,
                                      Console &con, FsMount **out)
    {
        if (!out)
            return FsStatus::InvalidArg;
        *out = nullptr;
        if (!parts || num_parts == 0)
        {
            return mount_auto(device, con, out);
        }
        for (size_t i = 0; i < num_parts; ++i)
        {
            const PartitionInfo &p = parts[i];
            con.printf("VFS: try partition %u: lba4k=%u blocks4k=%u\n",
                       (unsigned)i,
                       (unsigned long long)p.first_lba4k,
                       (unsigned long long)p.blocks4k);
            BlockDeviceSlice slice(device, p.first_lba4k, p.blocks4k);
            FsStatus st = try_mount_on(slice, con, out);
            if (st == FsStatus::Ok)
                return FsStatus::Ok;
        }
        return FsStatus::ProbeFailed;
    }

    bool mkdir(FsMount *mnt, const char *path, Console &con)
    {
        if (!mnt)
            return false;
        // FsMount の仮想関数を呼び出す
        return mnt->mkdir_path(path, con);
    }

    bool create(FsMount *mnt, const char *path, Console &con)
    {
        if (!mnt)
            return false;
        return mnt->create_path(path, con);
    }

    bool write(FsMount *mnt, const char *path, const void *buf, uint64_t len, uint64_t off, Console &con)
    {
        if (!mnt)
            return false;
        return mnt->write_path(path, buf, len, off, con);
    }

    bool read(FsMount *mnt, const char *path, void *buf, uint64_t len, uint64_t off, Console &con)
    {
        if (!mnt)
            return false;
        return mnt->read_path(path, buf, len, off, con);
    }

    bool stat(FsMount *mnt, const char *path, VfsStat &st, Console &con)
    {
        if (!mnt)
            return false;
        return mnt->stat_path(path, st, con);
    }

} // namespace vfs
