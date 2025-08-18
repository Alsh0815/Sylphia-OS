#include "../../../../include/std/cstring.hpp"
#include "../../../console.hpp"
#include "../../../kernel_runtime.hpp"
#include "../../../pmm.hpp"
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

    inline uint64_t fnv1a64(const char *s, size_t n, uint64_t seed)
    {
        uint64_t h = 0xcbf29ce484222325ULL ^ seed;
        const uint64_t p = 0x100000001b3ULL;
        for (size_t i = 0; i < n; ++i)
        {
            h ^= (uint8_t)s[i];
            h *= p;
        }
        return h;
    }

    inline uint32_t align_up_u32(uint32_t x, uint32_t a)
    {
        // a==0 は“アラインしない”扱い
        if (a == 0)
            return x;
        // 汎用（非2の累乗でも可）。オーバーフローしない範囲で使用する前提（本FSでは 4096 と 8 が主）
        uint32_t r = x % a;
        return r ? (x + (a - r)) : x;
    }

} // namespace

bool Sylph1FsDriver::probe(BlockDevice &device, Console &con)
{
    // SBはLBA4K=0固定の想定。4KiB読み出して検証。
    uint8_t *sb_buf = (uint8_t *)pmm::alloc_pages(1);
    if (!sb_buf)
    {
        con.println("Sylph1FS: probe failed to allocate buffer");
        return false;
    }

    // ScopeExit を使って、関数を抜ける際に必ずメモリを解放する
    ScopeExit free_buffer([&]()
                          { pmm::free_pages(sb_buf, 1); });
    if (!device.read_blocks_4k(/*lba4k*/ 0, /*count*/ 1, sb_buf, 4096, con))
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

bool Sylph1Mount::map_crc_entry(uint64_t data_idx, uint64_t &crc_lba4k, size_t &crc_off, Console &con) const
{
    // 境界チェック
    if (data_idx >= m_sb.data_area_blocks)
    {
        con.printf("Sylph1FS: CRC map out-of-range (data_idx=%u)\n",
                   (unsigned long long)data_idx);
        return false;
    }
    // サイドカー方式:
    //   crc_byte_off = data_idx * 4
    //   crc_lba4k    = crc_area_start + (crc_byte_off / 4096)
    //   crc_off      = crc_byte_off % 4096
    const uint64_t crc_byte_off = data_idx * 4ull;
    crc_lba4k = m_sb.crc_area_start + (crc_byte_off >> 12); // /4096
    crc_off = (size_t)(crc_byte_off & 0xFFFu);              // %4096

    // CRC area の範囲も軽く検査
    if (crc_lba4k < m_sb.crc_area_start ||
        crc_lba4k >= m_sb.crc_area_start + m_sb.crc_area_blocks)
    {
        con.println("Sylph1FS: CRC LBA out-of-range");
        return false;
    }
    // CRC は 4B なので、1ブロック内に収まる（4092/4093 を跨がない前提）
    if (crc_off > 4092)
    {
        con.println("Sylph1FS: CRC offset misaligned");
        return false;
    }
    return true;
}

bool Sylph1Mount::read_data_block(uint64_t data_idx, void *buf4096, Console &con) const
{
    if (!buf4096)
        return false;
    if (data_idx >= m_sb.data_area_blocks)
    {
        con.printf("Sylph1FS: read_data_block OOB (idx=%u)\n",
                   (unsigned long long)data_idx);
        return false;
    }

    const uint64_t data_lba4k = m_sb.data_area_start + data_idx;
    // データを読み出し
    if (!m_dev.read_blocks_4k(data_lba4k, 1, buf4096, 4096, con))
    {
        con.printf("Sylph1FS: read_data_block I/O error (LBA=%u)\n",
                   (unsigned long long)data_lba4k);
        return false;
    }

    // CRC 併読＋検証
    return verify_data_block_crc(data_idx, buf4096, con);
}

bool Sylph1Mount::verify_data_block_crc(uint64_t data_idx, const void *buf4096, Console &con) const
{
    uint64_t crc_lba4k = 0;
    size_t crc_off = 0;
    if (!map_crc_entry(data_idx, crc_lba4k, crc_off, con))
        return false;

    uint8_t *crcblk = (uint8_t *)pmm::alloc_pages(1);
    if (!crcblk)
    {
        con.println("Sylph1FS: verify_data_block_crc failed to allocate buffer");
        return false;
    }

    // ScopeExit を使って、関数を抜ける際に必ずメモリを解放する
    ScopeExit free_buffer([&]()
                          { pmm::free_pages(crcblk, 1); });
    if (!m_dev.read_blocks_4k(crc_lba4k, 1, crcblk, 4096, con))
    {
        con.printf("Sylph1FS: CRC read I/O error (LBA=%u)\n",
                   (unsigned long long)crc_lba4k);
        return false;
    }
    // 期待CRCをロード（little-endian 4B）
    uint32_t expected = 0;
    memcpy(&expected, crcblk + crc_off, sizeof(expected));

    // 実測CRC（Castagnoli）を計算
    const uint32_t actual = crc32c_reflected(buf4096, 4096);
    if (expected != actual)
    {
        con.printf("Sylph1FS: CRC mismatch at data_idx=%u (exp=%x act=%x)\n",
                   (unsigned long long)data_idx, (unsigned)expected, (unsigned)actual);
        return false;
    }
    return true;
}

bool Sylph1Mount::read_inode(uint64_t inode_id, sylph1fs::Inode &out, Console &con) const
{
    if (inode_id == 0 || inode_id > m_sb.total_inodes)
    {
        con.printf("Sylph1FS: read_inode invalid id=%llu\n",
                   (unsigned long long)inode_id);
        return false;
    }

    const uint64_t index = inode_id - 1;
    const uint64_t byte_off = index * 256ull;
    const uint64_t lba4k = m_sb.inode_table_start + (byte_off >> 12); // /4096
    const size_t off = (size_t)(byte_off & 0xFFFu);                   // %4096

    uint8_t *blk = (uint8_t *)pmm::alloc_pages(1);
    if (!blk)
    {
        con.println("Sylph1FS: read_inode failed to allocate buffer");
        return false;
    }

    // ScopeExit を使って、関数を抜ける際に必ずメモリを解放する
    ScopeExit free_buffer([&]()
                          { pmm::free_pages(blk, 1); });
    con.printf("DEBUG: Calling m_dev.read_blocks_4k from read_inode. m_dev is at %p\n", &m_dev);
    simple_wait(100000000);
    if (!m_dev.read_blocks_4k(lba4k, 1, blk, 4096, con))
    {
        con.printf("Sylph1FS: read_inode I/O error (LBA=%llu)\n",
                   (unsigned long long)lba4k);
        return false;
    }

    // 256B を抽出
    sylph1fs::Inode tmp{};
    memcpy(&tmp, blk + off, sizeof(tmp));

    // 末尾4B を期待値として取り出し、先頭252BのCRCと比較
    const uint32_t stored = tmp.inode_crc32c;
    tmp.inode_crc32c = 0; // 計算対象から除外（もしくは memcpy 前に別バッファで対応）
    const uint32_t calc = crc32c_reflected(&tmp, 252);

    if (stored != calc)
    {
        con.printf("Sylph1FS: inode CRC mismatch (id=%u exp=%x act=%x)\n",
                   (unsigned long long)inode_id, (unsigned)stored, (unsigned)calc);
        return false;
    }

    // OK
    out = tmp;
    return true;
}

bool Sylph1Mount::readdir_root(Console &con)
{
    con.println("DEBUG: Entered Sylph1Mount::readdir_root");
    sylph1fs::Inode ino{};
    if (!read_inode(1, ino, con))
    {
        con.println("Sylph1FS: readdir_root: failed to read inode #1");
        return false;
    }
    con.printf(".  (inode=%u)\n", 1u);
    con.printf(".. (inode=%u)\n", 1u);

    if (ino.dir_format != 1 || ino.dir_header_block >= m_sb.data_area_blocks)
    {
        con.println("(root dir: no header block; treated as empty)");
        return true;
    }

    // DirHeader 読み＋検証（既存）
    uint8_t *blk = (uint8_t *)pmm::alloc_pages(1);
    if (!blk)
    {
        con.println("Sylph1FS: readdir_root oom");
        return false;
    }
    ScopeExit free_hdr([&]()
                       { pmm::free_pages(blk, 1); });

    if (!read_data_block(ino.dir_header_block, blk, con))
    {
        con.println("Sylph1FS: readdir_root: failed to read dir header block");
        return false;
    }
    uint32_t stored = 0;
    memcpy(&stored, blk + 4096 - 4, 4);
    const uint32_t calc = crc32c_reflected(blk, 4096 - 4);
    if (stored != calc)
    {
        con.printf("Sylph1FS: dir header in-block CRC mismatch (exp=%x act=%x)\n",
                   (unsigned)stored, (unsigned)calc);
        return false;
    }

    const sylph1fs::DirHeader *hdr = reinterpret_cast<const sylph1fs::DirHeader *>(blk);
    if (hdr->magic != sylph1fs::kDirMagic || hdr->version != 1)
    {
        con.println("Sylph1FS: dir header invalid");
        return false;
    }

    const uint32_t bucket_count = hdr->bucket_count;
    con.printf("(root dir: buckets=%u, entries=%u)\n",
               (unsigned)bucket_count, (unsigned)hdr->entry_count); // 要約（既存）:contentReference[oaicite:3]{index=3}

    // === 実エントリ列挙 ===
    const uint32_t *buckets = reinterpret_cast<const uint32_t *>(blk + sizeof(sylph1fs::DirHeader));
    uint32_t listed = 0;

    for (uint32_t b = 0; b < bucket_count; ++b)
    {
        uint32_t slot = buckets[b];
        if (slot == sylph1fs::kBucketEmpty)
            continue;
        if (slot == sylph1fs::kBucketEmbedded)
        {
            con.println("Sylph1FS: embedded slab not implemented (skip)");
            continue;
        }
        // 外部スラブ鎖を辿る
        uint64_t slab_idx = slot;
        while (slab_idx != 0)
        {
            if (!enumerate_slab(slab_idx, con, listed))
                return false;

            // 次スラブを辿る
            uint8_t *slab = (uint8_t *)pmm::alloc_pages(1);
            if (!slab)
            {
                con.println("Sylph1FS: oom (follow)");
                return false;
            }
            ScopeExit free_tmp([&]()
                               { pmm::free_pages(slab, 1); });
            if (!read_data_block(slab_idx, slab, con))
                return false;

            const sylph1fs::DirSlabHeader *sh =
                reinterpret_cast<const sylph1fs::DirSlabHeader *>(slab);
            slab_idx = sh->next_block_rel;
        }
    }

    // 件数の整合性（参考：合わなければ警告）
    if (listed != hdr->entry_count)
    {
        con.printf("Sylph1FS: WARN entries mismatch header=%u actual=%u\n",
                   (unsigned)hdr->entry_count, (unsigned)listed);
    }
    return true;
}

bool Sylph1Mount::write_block_with_sidecar_crc(uint64_t data_idx, const void *buf4096, Console &con)
{
    const uint64_t data_lba = m_sb.data_area_start + data_idx;
    if (!m_dev.write_blocks_4k(data_lba, 1, buf4096, 4096, /*fua*/ true,
                               BlockDevice::kVerifyAfterWrite, con))
    {
        con.printf("Sylph1FS: write data LBA=%u failed\n", (unsigned long long)data_lba);
        return false;
    }

    uint64_t crc_lba;
    size_t crc_off;
    if (!map_crc_entry(data_idx, crc_lba, crc_off, con))
        return false;

    alignas(4096) uint8_t crcblk[4096];
    if (!m_dev.read_blocks_4k(crc_lba, 1, crcblk, sizeof(crcblk), con))
        return false;
    const uint32_t side_crc = crc32c_reflected(buf4096, 4096);
    memcpy(crcblk + crc_off, &side_crc, sizeof(side_crc));
    if (!m_dev.write_blocks_4k(crc_lba, 1, crcblk, sizeof(crcblk),
                               /*fua*/ true, BlockDevice::kVerifyAfterWrite, con))
    {
        con.printf("Sylph1FS: write sidecar CRC LBA=%u failed\n", (unsigned long long)crc_lba);
        return false;
    }
    return true;
}

// Data bitmap: first-fit 連続ラン確保（commitは別）
bool Sylph1Mount::alloc_data_blocks(uint32_t need, uint64_t &start_idx, Console &con)
{
    const uint64_t bits_per_block = 4096ull * 8ull; // 32768
    const uint64_t total = m_sb.data_area_blocks;
    uint64_t run = 0, run_start = 0;

    for (uint64_t bm_blk = 0; bm_blk < m_sb.bm_data_blocks; ++bm_blk)
    {
        alignas(4096) uint8_t bm[4096];
        if (!m_dev.read_blocks_4k(m_sb.bm_data_start + bm_blk, 1, bm, sizeof(bm), con))
            return false;

        for (uint64_t byte = 0; byte < 4096; ++byte)
        {
            uint8_t v = bm[byte];
            for (int b = 0; b < 8; ++b)
            {
                const uint64_t idx = bm_blk * bits_per_block + byte * 8 + b;
                if (idx >= total)
                    break;
                const bool used = (v >> b) & 1;
                if (!used)
                {
                    if (run == 0)
                        run_start = idx;
                    if (++run >= need)
                    {
                        start_idx = run_start;
                        return true;
                    }
                }
                else
                {
                    run = 0;
                }
            }
        }
    }
    con.println("Sylph1FS: alloc_data_blocks failed (no space)");
    return false;
}

bool Sylph1Mount::set_data_bitmap_range(uint64_t start_idx, uint32_t count, bool used, Console &con)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint64_t bit = start_idx + i;
        const uint64_t byte_index = bit >> 3;
        const uint8_t mask = (uint8_t)(1u << (bit & 7u));
        const uint64_t lba = m_sb.bm_data_start + (byte_index >> 12);
        const size_t off = (size_t)(byte_index & 0xFFFu);

        alignas(4096) uint8_t bm[4096];
        if (!m_dev.read_blocks_4k(lba, 1, bm, sizeof(bm), con))
            return false;
        if (used)
            bm[off] |= mask;
        else
            bm[off] &= (uint8_t)~mask;
        if (!m_dev.write_blocks_4k(lba, 1, bm, sizeof(bm), /*fua*/ true,
                                   BlockDevice::kVerifyAfterWrite, con))
            return false;
    }
    return true;
}

bool Sylph1Mount::alloc_inode(uint64_t &out_id, Console &con)
{
    const uint64_t bits_per_block = 4096ull * 8ull;
    for (uint64_t bm_blk = 0; bm_blk < m_sb.bm_inode_blocks; ++bm_blk)
    {
        alignas(4096) uint8_t bm[4096];
        if (!m_dev.read_blocks_4k(m_sb.bm_inode_start + bm_blk, 1, bm, sizeof(bm), con))
            return false;

        for (uint64_t byte = 0; byte < 4096; ++byte)
        {
            if (bm[byte] == 0xFF)
                continue;
            for (int b = 0; b < 8; ++b)
            {
                if (!(bm[byte] & (1u << b)))
                {
                    uint64_t id = bm_blk * bits_per_block + byte * 8 + b + 1; // 1起算
                    if (id == 0 || id > m_sb.total_inodes)
                        break;
                    out_id = id;
                    return true;
                }
            }
        }
    }
    con.println("Sylph1FS: alloc_inode failed (no space)");
    return false;
}

bool Sylph1Mount::set_inode_bitmap(uint64_t inode_id, bool used, Console &con)
{
    const uint64_t idx = inode_id - 1;
    const uint64_t byte_index = idx >> 3;
    const uint8_t mask = (uint8_t)(1u << (idx & 7u));
    const uint64_t lba = m_sb.bm_inode_start + (byte_index >> 12);
    const size_t off = (size_t)(byte_index & 0xFFFu);

    alignas(4096) uint8_t bm[4096];
    if (!m_dev.read_blocks_4k(lba, 1, bm, sizeof(bm), con))
        return false;
    if (used)
        bm[off] |= mask;
    else
        bm[off] &= (uint8_t)~mask;
    return m_dev.write_blocks_4k(lba, 1, bm, sizeof(bm), /*fua*/ true,
                                 BlockDevice::kVerifyAfterWrite, con);
}

bool Sylph1Mount::init_dir_block(uint32_t bucket_count, uint64_t &data_idx_out, Console &con)
{
    uint64_t idx = 0;
    if (!alloc_data_blocks(1, idx, con))
        return false;

    alignas(4096) uint8_t blk[4096];
    memset(blk, 0, sizeof(blk));

    sylph1fs::DirHeader *hdr = reinterpret_cast<sylph1fs::DirHeader *>(blk);
    hdr->magic = sylph1fs::kDirMagic;
    hdr->version = 1;
    hdr->bucket_count = bucket_count;
    hdr->entry_count = 0;

    // 末尾CRC（in-block）
    const uint32_t inblk_crc = crc32c_reflected(blk, 4096 - 4);
    memcpy(blk + 4096 - 4, &inblk_crc, sizeof(inblk_crc));

    // 本体→サイドカーCRC→bitmap の順で確定
    if (!write_block_with_sidecar_crc(idx, blk, con))
        return false;
    if (!set_data_bitmap_range(idx, 1, true, con))
        return false;

    data_idx_out = idx;
    return true;
}

bool Sylph1Mount::dir_add_entry_root(const char *name, uint16_t type, uint64_t child_ino, Console &con)
{
    if (!name)
        return false;
    const size_t nlen = strlen(name);
    if (nlen == 0 || nlen > 255)
    {
        con.println("Sylph1FS: invalid name");
        return false;
    }

    // ルート inode と ヘッダブロックを読む
    sylph1fs::Inode root{};
    if (!read_inode(1, root, con))
        return false;
    con.printf("DEBUG: dir_add_entry_root('%s') read inode #1, dir_header_block=%u\n", name, root.dir_header_block);
    if (root.dir_format != 1 || root.dir_header_block >= m_sb.data_area_blocks)
    {
        con.println("Sylph1FS: root dir is not hashed");
        return false;
    }
    const uint64_t hdr_idx = root.dir_header_block;

    alignas(4096) uint8_t hdrblk[4096];
    if (!read_data_block(hdr_idx, hdrblk, con))
        return false;
    // in-block CRC も検証
    uint32_t stored = 0;
    memcpy(&stored, hdrblk + 4096 - 4, 4);
    const uint32_t calc = crc32c_reflected(hdrblk, 4096 - 4);
    if (stored != calc)
    {
        con.println("Sylph1FS: dir header in-block CRC mismatch");
        return false;
    }
    sylph1fs::DirHeader *hdr = reinterpret_cast<sylph1fs::DirHeader *>(hdrblk);
    if (hdr->magic != sylph1fs::kDirMagic || hdr->version != 1)
        return false;

    // バケットを決める
    const uint64_t seed = 0; // v1はSBのdirhash_secret未使用なら0
    const uint64_t h = fnv1a64(name, nlen, seed);
    const uint32_t b = (hdr->bucket_count ? (uint32_t)(h % hdr->bucket_count) : 0);
    uint32_t *buckets = reinterpret_cast<uint32_t *>(hdrblk + sizeof(sylph1fs::DirHeader));
    uint32_t slot = buckets[b];

    // スラブの先頭を用意（空なら1ブロック新規確保）
    uint64_t slab_idx = 0;
    bool slab_is_external = true;
    if (slot == sylph1fs::kBucketEmpty)
    {
        // 1ブロック確保してスラブ化
        if (!alloc_data_blocks(1, slab_idx, con))
            return false;

        alignas(4096) uint8_t slab[4096];
        memset(slab, 0, sizeof(slab));
        sylph1fs::DirSlabHeader *sh = reinterpret_cast<sylph1fs::DirSlabHeader *>(slab);
        sh->used_bytes = sizeof(sylph1fs::DirSlabHeader);
        sh->entry_count = 0;
        sh->next_block_rel = 0;

        // 末尾CRC（in-block）
        uint32_t s_crc = crc32c_reflected(slab, 4096 - 4);
        memcpy(slab + 4096 - 4, &s_crc, 4);

        if (!write_block_with_sidecar_crc(slab_idx, slab, con))
            return false;
        // バケットに反映
        buckets[b] = (uint32_t)slab_idx;

        // ヘッダブロックの in-block CRC 更新 → 書戻し
        uint32_t h_crc = crc32c_reflected(hdrblk, 4096 - 4);
        memcpy(hdrblk + 4096 - 4, &h_crc, 4);
        if (!write_block_with_sidecar_crc(hdr_idx, hdrblk, con))
            return false;

        // データbitmap確定
        if (!set_data_bitmap_range(slab_idx, 1, true, con))
            return false;
    }
    else if (slot == sylph1fs::kBucketEmbedded)
    {
        con.println("Sylph1FS: embedded slab not implemented (yet)");
        return false;
    }
    else
    {
        slab_idx = slot;
    }

    // スラブブロックを読み、重複名チェック → 追記
    alignas(4096) uint8_t slab[4096];
    if (!read_data_block(slab_idx, slab, con))
        return false;
    uint32_t in_crc = 0;
    memcpy(&in_crc, slab + 4096 - 4, 4);
    if (in_crc != crc32c_reflected(slab, 4096 - 4))
    {
        con.println("Sylph1FS: slab in-block CRC mismatch");
        return false;
    }

    sylph1fs::DirSlabHeader *sh = reinterpret_cast<sylph1fs::DirSlabHeader *>(slab);
    uint32_t off = sh->used_bytes;
    // 既存走査（重複確認）
    {
        uint32_t p = sizeof(sylph1fs::DirSlabHeader);
        for (uint32_t i = 0; i < sh->entry_count && p + 8 <= 4096 - 4; ++i)
        {
            if (p + 8 > sh->used_bytes)
                break;
            uint16_t nlen2 = 0, type2 = 0;
            memcpy(&nlen2, slab + p + 0, 2);
            memcpy(&type2, slab + p + 2, 2);
            uint64_t ino2 = 0;
            memcpy(&ino2, slab + p + 4, 8);
            (void)type2;
            (void)ino2;
            if (p + 12 + nlen2 > 4096 - 4)
                break;
            bool same = (nlen2 == nlen) && (memcmp(slab + p + 12, name, nlen) == 0);
            // 8バイト境界にそろえて次へ
            uint32_t adv = align_up_u32((uint32_t)(12 + nlen2), 8);
            p += adv;
            if (same)
            {
                con.println("Sylph1FS: duplicate name");
                return false;
            }
        }
    }

    // 追記
    const uint32_t need = align_up_u32((uint32_t)(12 + nlen), 8);
    if (off + need > 4096 - 4)
    {
        con.println("Sylph1FS: slab is full (spill not implemented yet)");
        return false; // 将来: 新スラブを確保して next に繋ぐ
    }

    // [name_len(2)][type(2)][inode(8)][name...][pad]
    memcpy(slab + off + 0, &nlen, 2);
    memcpy(slab + off + 2, &type, 2);
    memcpy(slab + off + 4, &child_ino, 8);
    memcpy(slab + off + 12, name, nlen);
    // 残りは0で良い（パディング）
    // used/entry_count 更新
    sh->used_bytes = off + need;
    sh->entry_count = sh->entry_count + 1;

    // 末尾CRC更新 → 本体+サイドカーCRC 書戻し
    uint32_t s_crc = crc32c_reflected(slab, 4096 - 4);
    memcpy(slab + 4096 - 4, &s_crc, 4);
    if (!write_block_with_sidecar_crc(slab_idx, slab, con))
        return false;

    // メインヘッダの entry_count を更新して書き戻す
    hdr->entry_count = hdr->entry_count + 1;

    // ヘッダブロックの in-block CRC 更新 → 書戻し
    uint32_t h_crc = crc32c_reflected(hdrblk, 4096 - 4);
    memcpy(hdrblk + 4096 - 4, &h_crc, 4);
    if (!write_block_with_sidecar_crc(hdr_idx, hdrblk, con))
        return false;

    return true;
}

bool Sylph1Mount::test_create(const char *name, Console &con)
{
    if (m_ro)
    {
        con.println("Sylph1FS: read-only mount");
        return false;
    }

    // 1) inode を確保（テーブル書き→CRC→bitmapは最後）
    uint64_t ino_id = 0;
    if (!alloc_inode(ino_id, con))
        return false;

    // 2) 空ファイル inode を構築
    const uint64_t index = ino_id - 1;
    const uint64_t byte_off = index * 256ull;
    const uint64_t lba4k = m_sb.inode_table_start + (byte_off >> 12);
    const size_t off = (size_t)(byte_off & 0xFFFu);

    alignas(4096) uint8_t blk[4096];
    if (!m_dev.read_blocks_4k(lba4k, 1, blk, sizeof(blk), con))
        return false;
    memset(blk + off, 0, 256);

    sylph1fs::Inode *ino = reinterpret_cast<sylph1fs::Inode *>(blk + off);
    ino->inode_id = (uint64_t)ino_id;
    ino->mode = 0x8000 | 0644; // FILE
    ino->links = 1;
    ino->size_bytes = 0;
    ino->atime = ino->mtime = ino->ctime = 0;
    ino->extent_count = 0;
    ino->dir_format = 0;
    ino->dir_header_block = 0;
    ino->inode_crc32c = 0;
    ino->inode_crc32c = crc32c_reflected(ino, 252);

    if (!m_dev.write_blocks_4k(lba4k, 1, blk, sizeof(blk), /*fua*/ true,
                               BlockDevice::kVerifyAfterWrite, con))
        return false;

    // 3) ルートにエントリを追加
    if (!dir_add_entry_root(name, sylph1fs::kDirEntTypeFile, ino_id, con))
        return false;

    // 4) inode bitmap を確定
    if (!set_inode_bitmap(ino_id, true, con))
        return false;

    con.printf("Sylph1FS: created file \"%s\" (ino=%u)\n", name, (unsigned long long)ino_id);
    return true;
}

bool Sylph1Mount::test_mkdir(const char *name, Console &con)
{
    if (m_ro)
    {
        con.println("Sylph1FS: read-only mount");
        return false;
    }

    // 1) ディレクトリブロックを1つ作成
    uint64_t dir_idx = 0;
    if (!init_dir_block(/*bucket_count*/ 256, dir_idx, con))
        return false;

    // 2) inode を確保し、DIR inode を書き込む
    uint64_t ino_id = 0;
    if (!alloc_inode(ino_id, con))
        return false;

    const uint64_t index = ino_id - 1;
    const uint64_t byte_off = index * 256ull;
    const uint64_t lba4k = m_sb.inode_table_start + (byte_off >> 12);
    const size_t off = (size_t)(byte_off & 0xFFFu);

    alignas(4096) uint8_t blk[4096];
    if (!m_dev.read_blocks_4k(lba4k, 1, blk, sizeof(blk), con))
        return false;
    memset(blk + off, 0, 256);

    sylph1fs::Inode *ino = reinterpret_cast<sylph1fs::Inode *>(blk + off);
    ino->inode_id = (uint64_t)ino_id;
    ino->mode = 0x4000 | 0755; // DIR
    ino->links = 1;
    ino->size_bytes = 4096;
    ino->extent_count = 1;
    ino->extents_inline[0].start_block_rel = dir_idx;
    ino->extents_inline[0].length_blocks = 1;
    ino->dir_format = 1;
    ino->dir_header_block = dir_idx;
    ino->inode_crc32c = 0;
    ino->inode_crc32c = crc32c_reflected(ino, 252);

    if (!m_dev.write_blocks_4k(lba4k, 1, blk, sizeof(blk), /*fua*/ true,
                               BlockDevice::kVerifyAfterWrite, con))
        return false;

    // 3) ルートにエントリ追加
    if (!dir_add_entry_root(name, sylph1fs::kDirEntTypeDir, ino_id, con))
        return false;

    // 4) inode bitmap を確定
    if (!set_inode_bitmap(ino_id, true, con))
        return false;

    con.printf("Sylph1FS: created directory \"%s\" (ino=%u idx=%u)\n",
               name, (unsigned long long)ino_id, (unsigned long long)dir_idx);
    return true;
}

bool Sylph1Mount::enumerate_slab(uint64_t slab_idx, Console &con, uint32_t &out_count)
{
    // 読み出し＋サイドカーCRC検証
    uint8_t *slab = (uint8_t *)pmm::alloc_pages(1);
    if (!slab)
    {
        con.println("Sylph1FS: enumerate_slab oom");
        return false;
    }
    ScopeExit free_slab([&]()
                        { pmm::free_pages(slab, 1); });

    if (!read_data_block(slab_idx, slab, con))
        return false;

    // in-block CRC 検証
    uint32_t stored = 0;
    memcpy(&stored, slab + 4096 - 4, 4);
    const uint32_t calc = crc32c_reflected(slab, 4096 - 4);
    if (stored != calc)
    {
        con.println("Sylph1FS: slab in-block CRC mismatch");
        return false;
    }

    // ヘッダ
    const sylph1fs::DirSlabHeader *sh = reinterpret_cast<const sylph1fs::DirSlabHeader *>(slab);
    uint32_t used = sh->used_bytes;
    if (used < sizeof(sylph1fs::DirSlabHeader) || used > 4096 - 4)
    {
        con.println("Sylph1FS: slab used_bytes out-of-range");
        return false;
    }

    // 可変長エントリ列挙
    uint32_t p = sizeof(sylph1fs::DirSlabHeader);
    uint32_t local = 0;
    while (p + 12 <= used)
    {
        uint16_t nlen = 0, type = 0;
        uint64_t ino = 0;
        memcpy(&nlen, slab + p + 0, 2);
        memcpy(&type, slab + p + 2, 2);
        memcpy(&ino, slab + p + 4, 8);

        const uint32_t need = (uint32_t)(12u + nlen);
        if (need > 4096 - 4 || p + need > used)
            break; // 破損/終端保護

        const char *name = reinterpret_cast<const char *>(slab + p + 12);
        // 名前はそのまま出力（NUL終端ではない）
        // 表示用途：最大255までの範囲でプリント
        con.printf("%s %d %s (inode=%u)\n",
                   (type == sylph1fs::kDirEntTypeDir ? 'd' : 'f'),
                   (int)nlen, name, (unsigned long long)ino);

        ++local;
        // 8バイト境界まで前進
        const uint32_t adv = align_up_u32(need, 8);
        if (adv == 0)
            break;
        p += adv;
    }
    out_count += local;
    return true;
}

bool Sylph1Mount::lookup_in_root(const char *name, uint64_t &inode_out, uint16_t &type_out, Console &con)
{
    inode_out = 0;
    type_out = 0;
    if (!name)
        return false;

    const size_t nlen = strlen(name);
    if (nlen == 0 || nlen > 255)
    {
        con.println("Sylph1FS: lookup invalid name");
        return false;
    }

    // ルート inode とヘッダブロックの検証
    sylph1fs::Inode root{};
    if (!read_inode(1, root, con))
        return false;
    if (root.dir_format != 1 || root.dir_header_block >= m_sb.data_area_blocks)
    {
        con.println("Sylph1FS: root dir is not hashed");
        return false;
    }

    alignas(4096) uint8_t hdrblk[4096];
    if (!read_data_block(root.dir_header_block, hdrblk, con))
        return false;
    uint32_t stored = 0;
    memcpy(&stored, hdrblk + 4096 - 4, 4);
    const uint32_t calc = crc32c_reflected(hdrblk, 4096 - 4);
    if (stored != calc)
    {
        con.println("Sylph1FS: dir header in-block CRC mismatch");
        return false;
    }

    const sylph1fs::DirHeader *hdr = reinterpret_cast<const sylph1fs::DirHeader *>(hdrblk);
    if (hdr->magic != sylph1fs::kDirMagic || hdr->version != 1)
    {
        con.println("Sylph1FS: dir header invalid");
        return false;
    }

    const uint32_t bucket_count = hdr->bucket_count;
    if (bucket_count == 0)
        return false;

    // バケット選択（v1は seed=0 の FNV-1a 64）
    const uint64_t h = fnv1a64(name, nlen, /*seed*/ 0);
    const uint32_t b = (uint32_t)(h % bucket_count);
    const uint32_t *buckets =
        reinterpret_cast<const uint32_t *>(hdrblk + sizeof(sylph1fs::DirHeader));
    uint32_t slot = buckets[b];

    if (slot == sylph1fs::kBucketEmpty)
    {
        // 何も入っていない
        return false;
    }
    if (slot == sylph1fs::kBucketEmbedded)
    {
        // まだ未実装（将来：ヘッダブロック内の埋め込みスラブ）
        con.println("Sylph1FS: embedded slab not implemented");
        return false;
    }

    // 外部スラブ鎖をたどって完全一致を探す
    uint64_t slab_idx = slot;
    while (slab_idx != 0)
    {
        alignas(4096) uint8_t slab[4096];
        if (!read_data_block(slab_idx, slab, con))
            return false;

        uint32_t s_stored = 0;
        memcpy(&s_stored, slab + 4096 - 4, 4);
        const uint32_t s_calc = crc32c_reflected(slab, 4096 - 4);
        if (s_stored != s_calc)
        {
            con.println("Sylph1FS: slab in-block CRC mismatch");
            return false;
        }

        const sylph1fs::DirSlabHeader *sh =
            reinterpret_cast<const sylph1fs::DirSlabHeader *>(slab);

        uint32_t used = sh->used_bytes;
        if (used < sizeof(sylph1fs::DirSlabHeader) || used > 4096 - 4)
        {
            con.println("Sylph1FS: slab used_bytes out-of-range");
            return false;
        }

        uint32_t p = sizeof(sylph1fs::DirSlabHeader);
        for (uint32_t i = 0; i < sh->entry_count && p + 12 <= used; ++i)
        {
            uint16_t nlen2 = 0, type2 = 0;
            uint64_t ino2 = 0;
            memcpy(&nlen2, slab + p + 0, 2);
            memcpy(&type2, slab + p + 2, 2);
            memcpy(&ino2, slab + p + 4, 8);

            const uint32_t need = (uint32_t)(12u + nlen2);
            if (need > 4096 - 4 || p + need > used)
                break;

            if (nlen2 == nlen && memcmp(slab + p + 12, name, nlen) == 0)
            {
                inode_out = ino2;
                type_out = type2;
                return true;
            }

            const uint32_t adv = align_up_u32(need, 8);
            if (adv == 0)
                break;
            p += adv;
        }

        slab_idx = sh->next_block_rel;
    }

    return false;
}
