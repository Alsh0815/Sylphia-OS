#include "../../../../include/std/cstring.hpp"
#include "../../../console.hpp"
#include "sylph1fs_driver.hpp"

namespace
{
    // CRC32C (Castagnoli, poly=0x1EDC6F41 ; reflected=0x82F63B78)
    inline uint32_t crc32c_reflected(const void *data, size_t len)
    {
        const uint32_t poly = 0x82F63B78u;
        uint32_t crc = 0xFFFFFFFFu;
        const uint8_t *p = static_cast<const uint8_t *>(data);
        for (size_t i = 0; i < len; ++i)
        {
            crc ^= p[i];
            for (int b = 0; b < 8; ++b)
            {
                uint32_t mask = -(crc & 1u);
                crc = (crc >> 1) ^ (poly & mask);
            }
        }
        return crc ^ 0xFFFFFFFFu;
    }

    struct SbCheck
    {
        bool ok = false;
        bool clean = false;      // CLEANフラグ
        bool incomplete = false; // INCOMPLETEフラグ
    };

    // 最小マウントハンドル
    class Sylph1Mount : public FsMount
    {
    public:
        Sylph1Mount(BlockDevice &dev,
                    const sylph1fs::Superblock &sb,
                    bool read_only)
            : m_dev(dev), m_sb(sb), m_ro(read_only) {}

        void unmount(Console &con) override
        {
            // 読み取り専用でも flush は呼んでおく（BlockDevice側で無害）
            (void)con;
            m_dev.flush(con);
        }

        const sylph1fs::Superblock &superblock() const { return m_sb; }
        bool read_only() const { return m_ro; }

    private:
        BlockDevice &m_dev;
        sylph1fs::Superblock m_sb;
        bool m_ro;
    };

    static SbCheck read_and_validate_sb(BlockDevice &dev, uint64_t lba4k,
                                        sylph1fs::Superblock &out, Console &con)
    {
        alignas(4096) uint8_t buf[4096];
        if (!dev.read_blocks_4k(lba4k, 1, buf, sizeof(buf), con))
        {
            return {};
        }
        // メモリ上の構造体として扱う（packed前提）
        const sylph1fs::Superblock *sb = reinterpret_cast<const sylph1fs::Superblock *>(buf);

        // マジック/基本パラメタ
        const uint32_t kMagic = 0x53494C46u; // "SYLF"
        if (sb->magic != kMagic || sb->block_size_log2 != 12 || sb->csum_kind != 1)
        {
            return {};
        }
        // CRC32C(先頭4092B)
        const uint32_t expect = crc32c_reflected(buf, 4092);
        if (expect != sb->sb_crc32c)
        {
            return {};
        }
        // OK: outへコピー
        out = *sb;

        SbCheck r;
        r.ok = true;
        r.clean = ((sb->sb_flags & (1u << 0)) != 0);      // CLEAN
        r.incomplete = ((sb->sb_flags & (1u << 1)) != 0); // INCOMPLETE
        return r;
    }

    static bool features_supported(const sylph1fs::Superblock &sb, Console &con, bool &read_only)
    {
        // 互換: HAS_CHECKSUMS(bit0) は必須（CRCエリアを前提）
        if ((sb.features_compat & 1u) == 0)
        {
            con.println("Sylph1FS: missing required compat feature HAS_CHECKSUMS");
            return false;
        }
        // 非互換はv1では未使用のはず。非0ならマウント拒否。
        if (sb.features_incompat != 0)
        {
            con.println("Sylph1FS: has unsupported INCOMPAT features");
            return false;
        }
        // ro_compat はv1では0前提。将来はここで read_only=true に切替も可。
        (void)sb;
        (void)read_only;
        return true;
    }

    // SBの要約をログ
    static void log_sb_summary(const sylph1fs::Superblock &sb, Console &con, const char *tag)
    {
        // ラベルは32B固定・NUL非保証。長さを安全に計算。
        char label[33];
        size_t n = 0;
        while (n < 32 && sb.label[n] != '\0')
            ++n;
        for (size_t i = 0; i < n; ++i)
            label[i] = sb.label[i];
        label[n] = '\0';

        con.printf("Sylph1FS[%s]: v%u.%u blocks=%u inodes=%u data@%u+%u crc@%u+%u clean=%u incomplete=%u label=\"%s\"\n",
                   tag,
                   (unsigned)sb.version, (unsigned)sb.minor_version,
                   (unsigned long long)sb.total_blocks,
                   (unsigned long long)sb.total_inodes,
                   (unsigned long long)sb.data_area_start,
                   (unsigned long long)sb.data_area_blocks,
                   (unsigned long long)sb.crc_area_start,
                   (unsigned long long)sb.crc_area_blocks,
                   (unsigned)((sb.sb_flags >> 0) & 1),
                   (unsigned)((sb.sb_flags >> 1) & 1),
                   label);
    }

} // namespace

bool Sylph1FsDriver::probe(BlockDevice &device, Console &con)
{
    // SBはLBA4K=0固定の想定。4KiB読み出して検証。
    alignas(4096) uint8_t sb_buf[4096];
    if (!device.read_blocks_4k(/*lba4k*/ 0, /*count*/ 1, sb_buf, sizeof(sb_buf), con))
        return false;

    const sylph1fs::Superblock *sb = reinterpret_cast<const sylph1fs::Superblock *>(sb_buf);

    // 最低限の整合チェック
    const uint32_t kMagic = 0x53494C46u; // 'SYLF'
    if (sb->magic != kMagic)
        return false;
    if (sb->block_size_log2 != 12)
        return false; // 4KiB
    if (sb->csum_kind != 1)
        return false; // CRC32C

    // CRC32C(先頭4092B)を照合
    uint32_t expect = crc32c_reflected(sb_buf, 4092);
    if (expect != sb->sb_crc32c)
    {
        con.println("Sylph1FS: superblock CRC mismatch");
        return false;
    }

    // 追加の軽い sanity
    uint64_t total = device.logical_block_count_4k();
    if (sb->sb_backup_lba4k >= total)
        return false;

    return true;
}

FsMount *Sylph1FsDriver::mount(BlockDevice &device, Console &con)
{
    const uint64_t total = device.logical_block_count_4k();
    if (total < 2)
    {
        con.println("Sylph1FS: device too small");
        return nullptr;
    }

    sylph1fs::Superblock sb{};
    SbCheck p = read_and_validate_sb(device, /*primary*/ 0, sb, con);

    bool used_backup = false;
    if (!p.ok)
    {
        // Primaryが壊れている → Backupを試す
        sylph1fs::Superblock sb_b{};
        SbCheck b = read_and_validate_sb(device, /*backup*/ (total - 1), sb_b, con);
        if (!b.ok)
        {
            con.println("Sylph1FS: both primary and backup superblocks invalid");
            return nullptr;
        }
        used_backup = true;
        sb = sb_b;

        // 復旧（backup→primaryへ書戻し）を試みる。失敗してもROで継続。
        // CRCは sb_b に既に入っている。
        if (!device.write_blocks_4k(/*lba4k*/ 0, 1, &sb, sizeof(sb),
                                    /*fua*/ true, BlockDevice::kVerifyAfterWrite, con))
        {
            con.println("Sylph1FS: failed to repair primary SB (will continue RO)");
        }
        else
        {
            con.println("Sylph1FS: repaired primary SB from backup");
        }
    }

    // フラグでRO判定（INCOMPLETE=1ならRW禁止）
    bool read_only = false;
    if ((sb.sb_flags & (1u << 1)) != 0)
    { // INCOMPLETE
        con.println("Sylph1FS: INCOMPLETE flag set -> mounting read-only");
        read_only = true;
    }
    if (used_backup)
    {
        // backup経由で起動した場合も慎重にRO推奨
        con.println("Sylph1FS: using backup SB -> mounting read-only");
        read_only = true;
    }

    // feature確認
    if (!features_supported(sb, con, read_only))
    {
        return nullptr;
    }

    // 追加のサニティ：範囲
    if (sb.sb_backup_lba4k >= total)
    {
        con.println("Sylph1FS: sb_backup_lba4k out of range");
        return nullptr;
    }
    // crc/data領域も範囲内か軽く確認
    if (sb.crc_area_start + sb.crc_area_blocks > sb.sb_backup_lba4k)
    {
        con.println("Sylph1FS: CRC area overlaps backup SB");
        return nullptr;
    }

    log_sb_summary(sb, con, used_backup ? "backup" : "primary");

    // いまは最小ハンドル（SBとRO状態を保持）
    return new Sylph1Mount(device, sb, read_only);
}

bool register_sylph1fs_driver()
{
    static Sylph1FsDriver g_drv;
    return vfs::register_driver(&g_drv);
}
