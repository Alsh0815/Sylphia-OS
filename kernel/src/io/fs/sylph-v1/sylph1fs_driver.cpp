#include "../../../../include/std/algorithm.hpp"
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

    static void DebugPrintInode(const sylph1fs::Inode &ino, const char *tag, Console &con)
    {
        con.printf("--- Inode Debug (%s) ---\n", tag);
        con.printf("  ino_id=%u, size_bytes=%u, links=%u, extent_count=%u\n",
                   ino.inode_id, ino.size_bytes, ino.links, ino.extent_count);
        for (int i = 0; i < 4; ++i)
        {
            con.printf("  inline[%d]: start=%u, len=%u\n", i,
                       ino.extents_inline[i].start_block_rel, ino.extents_inline[i].length_blocks);
        }
        con.printf("  overflow_extents_block=%u\n", ino.overflow_extents_block);
        con.println("-------------------------");
    }

    inline uint64_t sx_div_floor(uint64_t x, uint64_t a) { return a ? (x / a) : 0; }
    inline uint64_t sx_mod(uint64_t x, uint64_t a) { return a ? (x % a) : 0; }

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

static inline bool try_coalesce_last(
    PmmVec<sylph1fs::Extent> &list,
    const sylph1fs::Extent &e)
{
    if (list.empty())
        return false;
    auto &last = list.back();
    const uint64_t last_end = last.start_block_rel + last.length_blocks;
    if (last_end == e.start_block_rel &&
        last.length_blocks < 0xFFFFFFFFu)
    {
        const uint64_t new_len = (uint64_t)last.length_blocks + (uint64_t)e.length_blocks;
        last.length_blocks = (new_len > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)new_len;
        return true;
    }
    return false;
}

bool Sylph1Mount::check_dir_crc(uint64_t inode_id, const char *name, size_t &nlen, sylph1fs::Inode &parent, sylph1fs::DirHeader *&hdr, uint8_t *hdrblk, uint64_t &hdr_idx, Console &con)
{
    if (!name)
        return false;
    nlen = strlen(name);
    if (nlen == 0 || nlen > 255)
    {
        con.println("Sylph1FS: invalid name");
        return false;
    }

    parent = sylph1fs::Inode{};
    if (!read_inode(inode_id, parent, con))
        return false;
    if (parent.dir_format != 1 || parent.dir_header_block >= m_sb.data_area_blocks)
    {
        con.println("Sylph1FS: parent not hashed dir");
        return false;
    }
    hdr_idx = parent.dir_header_block;

    if (!read_data_block(hdr_idx, hdrblk, con))
        return false;
    uint32_t h_stored = 0;
    memcpy(&h_stored, hdrblk + 4096 - 4, 4);
    if (h_stored != crc32c_reflected(hdrblk, 4096 - 4))
    {
        con.println("Sylph1FS: dir header CRC mismatch");
        return false;
    }

    hdr = reinterpret_cast<sylph1fs::DirHeader *>(hdrblk);
    if (hdr->magic != sylph1fs::kDirMagic || hdr->version != 1)
        return false;

    return true;
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
    // con.printf("DEBUG: Calling m_dev.read_blocks_4k from read_inode. m_dev is at %p\n", &m_dev);
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

// 修正後
bool Sylph1Mount::set_data_bitmap_range(uint64_t start_idx, uint32_t count, bool used, Console &con)
{
    if (count == 0)
        return true;

    const uint64_t bits_per_block = 4096ull * 8ull;
    const uint64_t end_idx = start_idx + count - 1;

    // 影響を受けるビットマップブロックの範囲を計算
    const uint64_t start_bm_block = start_idx / bits_per_block;
    const uint64_t end_bm_block = end_idx / bits_per_block;

    uint8_t *bm_buf = (uint8_t *)pmm::alloc_pages(1);
    if (!bm_buf)
    {
        con.println("Sylph1FS: set_data_bitmap_range failed to allocate buffer");
        return false;
    }
    ScopeExit free_buffer([&]()
                          { pmm::free_pages(bm_buf, 1); });

    // 影響を受けるブロックごとにループ
    for (uint64_t bm_block_offset = start_bm_block; bm_block_offset <= end_bm_block; ++bm_block_offset)
    {
        const uint64_t lba = m_sb.bm_data_start + bm_block_offset;

        // (a) ブロックを丸ごと読み込む
        if (!m_dev.read_blocks_4k(lba, 1, bm_buf, 4096, con))
        {
            return false;
        }

        // (b) このブロック内で変更すべきビットを全てメモリ上で変更
        const uint64_t block_start_bit = bm_block_offset * bits_per_block;
        const uint64_t first_bit_in_block = (bm_block_offset == start_bm_block) ? start_idx : block_start_bit;
        const uint64_t last_bit_in_block = (bm_block_offset == end_bm_block) ? end_idx : block_start_bit + bits_per_block - 1;

        for (uint64_t bit = first_bit_in_block; bit <= last_bit_in_block; ++bit)
        {
            const uint64_t bit_in_block = bit - block_start_bit;
            const uint64_t byte_in_block = bit_in_block / 8;
            const uint8_t mask = (uint8_t)(1u << (bit_in_block % 8));

            if (used)
            {
                bm_buf[byte_in_block] |= mask;
            }
            else
            {
                bm_buf[byte_in_block] &= (uint8_t)~mask;
            }
        }

        // (c) 変更したブロックを丸ごと書き戻す
        if (!m_dev.write_blocks_4k(lba, 1, bm_buf, 4096, /*fua*/ true,
                                   BlockDevice::kVerifyAfterWrite, con))
        {
            return false;
        }
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

// 修正後
bool Sylph1Mount::set_inode_bitmap(uint64_t inode_id, bool used, Console &con)
{
    const uint64_t idx = inode_id - 1;
    const uint64_t byte_index = idx / 8;
    const uint8_t mask = (uint8_t)(1u << (idx % 8));
    const uint64_t lba = m_sb.bm_inode_start + (byte_index / 4096);
    const size_t off_in_block = (size_t)(byte_index % 4096);

    // pmmからバッファを確保
    uint8_t *bm_buf = (uint8_t *)pmm::alloc_pages(1);
    if (!bm_buf)
    {
        con.println("Sylph1FS: set_inode_bitmap failed to allocate buffer");
        return false;
    }
    // ScopeExitで解放を保証
    ScopeExit free_buffer([&]()
                          { pmm::free_pages(bm_buf, 1); });

    // ブロックを一度読み込む
    if (!m_dev.read_blocks_4k(lba, 1, bm_buf, 4096, con))
    {
        return false;
    }

    // メモリ上でビットを操作
    if (used)
    {
        bm_buf[off_in_block] |= mask;
    }
    else
    {
        bm_buf[off_in_block] &= (uint8_t)~mask;
    }

    // ブロックを一度書き戻す
    return m_dev.write_blocks_4k(lba, 1, bm_buf, 4096, /*fua*/ true,
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

        if (type != 0) // type が 0 (削除済み) でない有効なエントリのみ処理する
        {
            const char *name = reinterpret_cast<const char *>(slab + p + 12);
            char name_buf[256];
            memcpy(name_buf, name, nlen);
            name_buf[nlen] = '\0';
            con.printf("  %s  (inode=%u, type=%c)\n",
                       name_buf, (unsigned long long)ino,
                       (type == sylph1fs::kDirEntTypeDir ? 'd' : 'f'));

            ++local;
        }
        // 8バイト境界まで前進
        const uint32_t adv = align_up_u32(need, 8);
        if (adv == 0)
            break;
        p += adv;
    }
    out_count += local;
    return true;
}

bool Sylph1Mount::append_entry_with_spill(uint64_t slab_idx,
                                          const char *name, uint16_t type, uint64_t child_ino,
                                          bool *out_spilled,
                                          Console &con)
{
    if (out_spilled)
        *out_spilled = false;
    // --- 追記サイズ計算 ---
    const uint32_t nlen = (uint32_t)strlen(name);
    const uint32_t need = align_up_u32(12u + nlen, 8u); // [hdr12 + name] を8バイト境界へ
    if (nlen == 0 || nlen > 255)
    {
        con.println("Sylph1FS: invalid name");
        return false;
    }

    // スラブ鎖の末尾まで辿って、空きのある場所に追記
    uint64_t cur = slab_idx;
    bool allocated_new_slab = false;
    for (;;)
    {
        alignas(4096) uint8_t slab[4096];
        if (!read_data_block(cur, slab, con))
            return false;

        // in-block CRC 検証
        uint32_t s_stored = 0;
        memcpy(&s_stored, slab + 4096 - 4, 4);
        if (s_stored != crc32c_reflected(slab, 4096 - 4))
        {
            con.println("Sylph1FS: slab in-block CRC mismatch");
            return false;
        }

        sylph1fs::DirSlabHeader *sh = reinterpret_cast<sylph1fs::DirSlabHeader *>(slab);
        if (sh->used_bytes < sizeof(sylph1fs::DirSlabHeader) || sh->used_bytes > 4096u - 4u)
        {
            con.println("Sylph1FS: slab used_bytes out-of-range");
            return false;
        }

        // --- 空きがあればここに追記 ---
        if (sh->used_bytes + need <= 4096u - 4u)
        {
            const uint32_t off = sh->used_bytes;
            // 既存：重複名チェック（軽量版；このスラブ内だけ）
            {
                uint32_t p = sizeof(sylph1fs::DirSlabHeader);
                for (uint32_t i = 0; i < sh->entry_count && p + 12 <= sh->used_bytes; ++i)
                {
                    uint16_t nlen2 = 0;
                    memcpy(&nlen2, slab + p + 0, 2);
                    if (nlen2 == nlen && memcmp(slab + p + 12, name, nlen) == 0)
                    {
                        con.println("Sylph1FS: duplicate name");
                        return false;
                    }
                    const uint32_t adv = align_up_u32(12u + (uint32_t)nlen2, 8u);
                    if (!adv)
                        break;
                    p += adv;
                }
            }

            // [name_len(2)][type(2)][inode(8)][name…][pad]
            memcpy(slab + off + 0, &nlen, 2);
            memcpy(slab + off + 2, &type, 2);
            memcpy(slab + off + 4, &child_ino, 8);
            memcpy(slab + off + 12, name, nlen);

            sh->used_bytes = off + need;
            sh->entry_count = sh->entry_count + 1;

            // in-block CRC を更新 → 書戻し（＋サイドカーCRC）
            uint32_t s_crc = crc32c_reflected(slab, 4096 - 4);
            memcpy(slab + 4096 - 4, &s_crc, 4);
            if (!write_block_with_sidecar_crc(cur, slab, con))
                return false;

            if (out_spilled)
                *out_spilled = allocated_new_slab;
            return true;
        }

        // --- 空きがない：次のスラブがあればそちらへ ---
        if (sh->next_block_rel != 0)
        {
            cur = sh->next_block_rel;
            continue;
        }

        // --- 空きがない：次のスラブがない → 新規スラブを確保して連結 ---
        uint64_t new_idx = 0;
        if (!alloc_data_blocks(1, new_idx, con))
            return false;

        // 新スラブを初期化
        alignas(4096) uint8_t new_slab[4096];
        memset(new_slab, 0, sizeof(new_slab));
        sylph1fs::DirSlabHeader *nsh = reinterpret_cast<sylph1fs::DirSlabHeader *>(new_slab);
        nsh->used_bytes = sizeof(sylph1fs::DirSlabHeader);
        nsh->entry_count = 0;
        nsh->next_block_rel = 0;
        uint32_t ns_crc = crc32c_reflected(new_slab, 4096 - 4);
        memcpy(new_slab + 4096 - 4, &ns_crc, 4);
        if (!write_block_with_sidecar_crc(new_idx, new_slab, con))
            return false;

        // 旧スラブに new_idx を連結
        sh->next_block_rel = new_idx;
        uint32_t s_crc2 = crc32c_reflected(slab, 4096 - 4);
        memcpy(slab + 4096 - 4, &s_crc2, 4);
        if (!write_block_with_sidecar_crc(cur, slab, con))
            return false;

        // 新ブロックの data bitmap を **最後に** 立てる
        if (!set_data_bitmap_range(new_idx, 1, true, con))
            return false;

        // ループ続行：新スラブに追記
        cur = new_idx;
        allocated_new_slab = true;
        // and loop…
    }
}

bool Sylph1Mount::lookup_in_dir(uint64_t dir_inode_id, const char *name,
                                uint64_t &inode_out, uint16_t &type_out, Console &con)
{
    inode_out = 0;
    type_out = 0;
    size_t nlen = 0;
    uint64_t hdr_idx = 0;
    sylph1fs::Inode parent{};
    alignas(4096) uint8_t hdrblk[4096];
    sylph1fs::DirHeader *hdr;
    if (!check_dir_crc(dir_inode_id, name, nlen, parent, hdr, hdrblk, hdr_idx, con))
        return false;

    const uint32_t bucket_count = hdr->bucket_count;
    if (!bucket_count)
        return false;

    const uint64_t h = fnv1a64(name, nlen, /*seed*/ 0);
    const uint32_t b = (uint32_t)(h % bucket_count);
    const uint32_t *buckets = reinterpret_cast<const uint32_t *>(hdrblk + sizeof(sylph1fs::DirHeader));
    uint32_t slot = buckets[b];

    if (slot == sylph1fs::kBucketEmpty)
        return false;
    if (slot == sylph1fs::kBucketEmbedded)
    {
        con.println("Sylph1FS: embedded slab not implemented");
        return false;
    }

    uint64_t slab_idx = slot;
    while (slab_idx != 0)
    {
        alignas(4096) uint8_t slab[4096];
        if (!read_data_block(slab_idx, slab, con))
            return false;

        uint32_t s_stored = 0;
        memcpy(&s_stored, slab + 4096 - 4, 4);
        if (s_stored != crc32c_reflected(slab, 4096 - 4))
        {
            con.println("Sylph1FS: slab CRC mismatch");
            return false;
        }

        const sylph1fs::DirSlabHeader *sh = reinterpret_cast<const sylph1fs::DirSlabHeader *>(slab);
        uint32_t used = sh->used_bytes;
        if (used < sizeof(sylph1fs::DirSlabHeader) || used > 4096 - 4)
        {
            con.println("Sylph1FS: slab used out-of-range");
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
            if (!adv)
                break;
            p += adv;
        }
        slab_idx = sh->next_block_rel;
    }
    return false;
}

bool Sylph1Mount::dir_add_entry(uint64_t parent_inode_id, const char *name,
                                uint16_t type, uint64_t child_ino, Console &con)
{

    size_t nlen = 0;
    uint64_t hdr_idx = 0;
    sylph1fs::Inode parent{};
    alignas(4096) uint8_t hdrblk[4096];
    sylph1fs::DirHeader *hdr;
    if (!check_dir_crc(parent_inode_id, name, nlen, parent, hdr, hdrblk, hdr_idx, con))
        return false;

    // 既に存在？
    uint64_t dummy_ino = 0;
    uint16_t dummy_ty = 0;
    if (lookup_in_dir(parent_inode_id, name, dummy_ino, dummy_ty, con))
    {
        con.println("Sylph1FS: name already exists");
        return false;
    }

    const uint64_t h = fnv1a64(name, nlen, 0);
    const uint32_t b = (uint32_t)(h % hdr->bucket_count);
    uint32_t *buckets = reinterpret_cast<uint32_t *>(hdrblk + sizeof(sylph1fs::DirHeader));
    uint32_t slot = buckets[b];

    // バケット未使用なら新規スラブを1個確保
    uint64_t slab_idx = 0;
    if (slot == sylph1fs::kBucketEmpty)
    {
        if (!alloc_data_blocks(1, slab_idx, con))
            return false;

        alignas(4096) uint8_t slab[4096];
        memset(slab, 0, sizeof(slab));
        sylph1fs::DirSlabHeader *sh = reinterpret_cast<sylph1fs::DirSlabHeader *>(slab);
        sh->used_bytes = sizeof(sylph1fs::DirSlabHeader);
        sh->entry_count = 0;
        sh->next_block_rel = 0;

        uint32_t s_crc = crc32c_reflected(slab, 4096 - 4);
        memcpy(slab + 4096 - 4, &s_crc, 4);
        if (!write_block_with_sidecar_crc(slab_idx, slab, con))
            return false;

        buckets[b] = (uint32_t)slab_idx;
        uint32_t h_crc = crc32c_reflected(hdrblk, 4096 - 4);
        memcpy(hdrblk + 4096 - 4, &h_crc, 4);
        if (!write_block_with_sidecar_crc(hdr_idx, hdrblk, con))
            return false;

        if (!set_data_bitmap_range(slab_idx, 1, true, con))
            return false;

        if (parent.size_bytes < UINT64_MAX - 4096)
        {
            parent.size_bytes += 4096;
            if (!write_inode(parent, con))
                return false;
        }
    }
    else if (slot == sylph1fs::kBucketEmbedded)
    {
        con.println("Sylph1FS: embedded slab not implemented");
        return false;
    }
    else
    {
        slab_idx = slot;
    }

    // 追記（満杯なら内部でスピル）
    bool spilled = false;
    if (!append_entry_with_spill(slab_idx, name, type, child_ino, &spilled, con))
        return false;

    // ヘッダ entry_count を+1
    if (!read_data_block(hdr_idx, hdrblk, con))
        return false;
    sylph1fs::DirHeader *hdr2 = reinterpret_cast<sylph1fs::DirHeader *>(hdrblk);
    hdr2->entry_count = hdr2->entry_count + 1;
    uint32_t h_crc2 = crc32c_reflected(hdrblk, 4096 - 4);
    memcpy(hdrblk + 4096 - 4, &h_crc2, 4);
    if (!write_block_with_sidecar_crc(hdr_idx, hdrblk, con))
        return false;
    if (spilled)
    {
        if (parent.size_bytes < UINT64_MAX - 4096)
        {
            parent.size_bytes += 4096;
            if (!write_inode(parent, con))
                return false;
        }
    }
    return true;
}

bool Sylph1Mount::mkdir_path(const char *abs_path, Console &con)
{
    if (m_ro)
    {
        con.println("Sylph1FS: read-only mount");
        return false;
    }

    char name[256];
    size_t nlen = 0;
    uint64_t parent = 0;
    if (!split_parent_basename(abs_path, parent, name, nlen, con))
        return false;

    // 既存チェック
    uint64_t exist = 0;
    uint16_t ety = 0;
    if (lookup_in_dir(parent, name, exist, ety, con))
    {
        con.println("Sylph1FS: mkdir: already exists");
        return false;
    }

    // 1) 子ディレクトリ用のヘッダブロックを1つ作成
    uint64_t dir_idx = 0;
    if (!init_dir_block(/*bucket_count*/ 256, dir_idx, con))
        return false;

    // 2) inode を確保し、DIR inode を書き込み（links=1 で仮置き）
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
    ino->links = 1;            // ← 後で 2 に更新
    ino->size_bytes = 4096;
    ino->extent_count = 1;
    ino->extents_inline[0].start_block_rel = dir_idx;
    ino->extents_inline[0].length_blocks = 1;
    ino->dir_format = 1;
    ino->dir_header_block = dir_idx;
    ino->inode_crc32c = 0;
    ino->inode_crc32c = crc32c_reflected(ino, 252);

    if (!m_dev.write_blocks_4k(lba4k, 1, blk, sizeof(blk),
                               /*fua*/ true, BlockDevice::kVerifyAfterWrite, con))
        return false;

    // 3) 親へ登録（spill対応）
    if (!dir_add_entry(parent, name, sylph1fs::kDirEntTypeDir, ino_id, con))
        return false;

    // 4) 子ディレクトリ自身に "." と ".." を実体として追加
    if (!dir_add_entry(ino_id, ".", sylph1fs::kDirEntTypeDir, ino_id, con))
        return false;
    if (!dir_add_entry(ino_id, "..", sylph1fs::kDirEntTypeDir, parent, con))
        return false;

    // 5) link count を更新
    //   子: 1 → 2（"." エントリで+1）
    sylph1fs::Inode child{};
    if (!read_inode(ino_id, child, con))
        return false;
    child.links = 2;
    if (!write_inode(child, con))
        return false;

    //   親: 子の ".." により +1
    sylph1fs::Inode pin{};
    if (!read_inode(parent, pin, con))
        return false;
    pin.links = pin.links + 1;
    if (!write_inode(pin, con))
        return false;

    // 6) inode bitmap を最後に確定
    if (!set_inode_bitmap(ino_id, true, con))
        return false;

    con.printf("Sylph1FS: mkdir '%s' under ino=%llu -> ino=%llu idx=%llu (links: parent=%u child=%u)\n",
               name,
               (unsigned long long)parent,
               (unsigned long long)ino_id,
               (unsigned long long)dir_idx,
               (unsigned)pin.links,
               (unsigned)child.links);
    return true;
}

bool Sylph1Mount::create_path(const char *abs_path, Console &con)
{
    if (m_ro)
    {
        con.println("Sylph1FS: read-only mount");
        return false;
    }

    char name[256];
    size_t nlen = 0;
    uint64_t parent = 0;
    if (!split_parent_basename(abs_path, parent, name, nlen, con))
        return false;

    uint64_t exist = 0;
    uint16_t ety = 0;
    if (lookup_in_dir(parent, name, exist, ety, con))
    {
        con.println("Sylph1FS: create: already exists");
        return false;
    }

    // inode を確保して空ファイルを書き込み
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
    ino->mode = 0x8000 | 0644; // FILE
    ino->links = 1;
    ino->size_bytes = 0;
    ino->extent_count = 0;
    ino->dir_format = 0;
    ino->dir_header_block = 0;
    ino->inode_crc32c = 0;
    ino->inode_crc32c = crc32c_reflected(ino, 252);

    if (!m_dev.write_blocks_4k(lba4k, 1, blk, sizeof(blk),
                               /*fua*/ true, BlockDevice::kVerifyAfterWrite, con))
        return false;

    // 親へ登録（spill対応）
    if (!dir_add_entry(parent, name, sylph1fs::kDirEntTypeFile, ino_id, con))
        return false;

    // inode bitmap を確定
    if (!set_inode_bitmap(ino_id, true, con))
        return false;

    con.printf("Sylph1FS: create '%s' under ino=%u -> ino=%u\n",
               name, (unsigned long long)parent, (unsigned long long)ino_id);
    return true;
}

bool Sylph1Mount::split_parent_basename(const char *abs_path,
                                        uint64_t &parent_ino,
                                        char *base_out, size_t &base_len,
                                        Console &con)
{
    parent_ino = 1; // root
    base_len = 0;
    if (!abs_path || abs_path[0] != '/')
        return false;

    // スラッシュを区切りに走査。最後の要素を base に、それ以外は親を辿る。
    const char *p = abs_path;
    // skip leading '/'
    while (*p == '/')
        ++p;
    if (*p == '\0')
        return false; // "/" は不可（basenameなし）

    char seg[256];
    while (*p)
    {
        // 1セグメント抽出
        size_t n = 0;
        while (p[n] && p[n] != '/' && n < 255)
        {
            seg[n] = p[n];
            ++n;
        }
        seg[n] = '\0';

        // 次の位置
        const char *next = p + n;
        while (*next == '/')
            ++next; // 連続'/'をスキップ
        const bool is_last = (*next == '\0');

        if (n == 0)
        { // "//" のような空セグメントは無視
            p = next;
            continue;
        }
        // "." はスキップ、".." は v1 では未対応（root の親などの扱いが無い）
        if (n == 1 && seg[0] == '.')
        {
            p = next;
            continue;
        }
        if (n == 2 && seg[0] == '.' && seg[1] == '.')
        {
            con.println("Sylph1FS: '..' in path not supported yet");
            return false;
        }

        if (is_last)
        {
            // 末端：basename として返す
            memcpy(base_out, seg, n);
            base_out[n] = '\0';
            base_len = n;
            return true;
        }
        else
        {
            // 中間：親ディレクトリを辿る
            uint64_t child = 0;
            uint16_t ty = 0;
            if (!lookup_in_dir(parent_ino, seg, child, ty, con))
            {
                con.printf("Sylph1FS: parent segment '%s' not found\n", seg);
                return false;
            }
            if (ty != sylph1fs::kDirEntTypeDir)
            {
                con.printf("Sylph1FS: '%s' is not a directory\n", seg);
                return false;
            }
            parent_ino = child;
            p = next;
        }
    }
    return false; // 正常なら上の is_last に入る
}

bool Sylph1Mount::readdir_dir(uint64_t dir_inode_id, Console &con)
{
    sylph1fs::Inode ino{};
    if (!read_inode(dir_inode_id, ino, con))
    {
        con.printf("Sylph1FS: readdir_dir: failed to read inode #%u\n",
                   (unsigned long long)dir_inode_id);
        return false;
    }
    if (ino.dir_format != 1 || ino.dir_header_block >= m_sb.data_area_blocks)
    {
        con.println("Sylph1FS: readdir_dir: target is not a hashed directory");
        return false;
    }

    // DirHeader 読み＋CRC検証（以下は従来どおり）
    alignas(4096) uint8_t blk[4096];
    if (!read_data_block(ino.dir_header_block, blk, con))
        return false;

    uint32_t stored = 0;
    memcpy(&stored, blk + 4096 - 4, 4);
    const uint32_t calc = crc32c_reflected(blk, 4096 - 4);
    if (stored != calc)
    {
        con.println("Sylph1FS: dir header in-block CRC mismatch");
        return false;
    }

    const sylph1fs::DirHeader *hdr =
        reinterpret_cast<const sylph1fs::DirHeader *>(blk);
    if (hdr->magic != sylph1fs::kDirMagic || hdr->version != 1)
    {
        con.println("Sylph1FS: dir header invalid");
        return false;
    }

    const uint32_t bucket_count = hdr->bucket_count;
    con.printf("(dir ino=%u: buckets=%u, entries=%u)\n",
               (unsigned long long)dir_inode_id,
               (unsigned)bucket_count, (unsigned)hdr->entry_count);

    const uint32_t *buckets =
        reinterpret_cast<const uint32_t *>(blk + sizeof(sylph1fs::DirHeader));
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
        uint64_t slab_idx = slot;
        while (slab_idx != 0)
        {
            if (!enumerate_slab(slab_idx, con, listed))
                return false;

            alignas(4096) uint8_t slab[4096];
            if (!read_data_block(slab_idx, slab, con))
                return false;
            const sylph1fs::DirSlabHeader *sh =
                reinterpret_cast<const sylph1fs::DirSlabHeader *>(slab);
            slab_idx = sh->next_block_rel;
        }
    }

    if (listed != hdr->entry_count)
    {
        con.printf("Sylph1FS: WARN entries mismatch header=%u actual=%u\n",
                   (unsigned)hdr->entry_count, (unsigned)listed);
    }
    return true;
}

bool Sylph1Mount::resolve_path_inode(const char *abs_path,
                                     uint64_t &inode_out, uint16_t &type_out, Console &con)
{
    inode_out = 0;
    type_out = 0;
    if (!abs_path || abs_path[0] != '/')
        return false;

    // ルート
    uint64_t cur = 1;
    uint16_t cur_ty = sylph1fs::kDirEntTypeDir;

    const char *p = abs_path;
    while (*p == '/')
        ++p; // 先頭の'/'群をスキップ
    if (*p == '\0')
    { // "/" はルート
        inode_out = cur;
        type_out = cur_ty;
        return true;
    }

    char seg[256];
    while (*p)
    {
        // セグメント抽出
        size_t n = 0;
        while (p[n] && p[n] != '/' && n < 255)
        {
            seg[n] = p[n];
            ++n;
        }
        seg[n] = '\0';

        const char *next = p + n;
        while (*next == '/')
            ++next;
        const bool is_last = (*next == '\0');

        if (n == 0)
        {
            p = next;
            continue;
        }
        if (n == 1 && seg[0] == '.')
        {
            p = next;
            continue;
        }
        if (n == 2 && seg[0] == '.' && seg[1] == '.')
        {
            con.println("Sylph1FS: '..' in path not supported yet");
            return false;
        }

        // 現在位置がディレクトリである必要
        if (cur_ty != sylph1fs::kDirEntTypeDir)
        {
            con.println("Sylph1FS: path walks into non-directory");
            return false;
        }

        uint64_t next_ino = 0;
        uint16_t next_ty = 0;
        if (!lookup_in_dir(cur, seg, next_ino, next_ty, con))
        {
            if (is_last)
            {
                // 最終要素が存在しない場合、呼び出し側のポリシにより NotFound とする
                return false;
            }
            con.printf("Sylph1FS: path segment '%s' not found\n", seg);
            return false;
        }

        cur = next_ino;
        cur_ty = next_ty;
        p = next;
    }

    inode_out = cur;
    type_out = cur_ty;
    return true;
}

bool Sylph1Mount::readdir_path(const char *abs_path, Console &con)
{
    uint64_t ino = 0;
    uint16_t ty = 0;
    if (!resolve_path_inode(abs_path, ino, ty, con))
    {
        con.printf("Sylph1FS: readdir_path: resolve failed for '%s'\n",
                   abs_path ? abs_path : "(null)");
        return false;
    }
    if (ty != sylph1fs::kDirEntTypeDir)
    {
        con.printf("Sylph1FS: readdir_path: '%s' is not a directory\n", abs_path);
        return false;
    }
    return readdir_dir(ino, con);
}

bool Sylph1Mount::write_inode(const sylph1fs::Inode &ino, Console &con)
{
    const uint64_t inode_id = ino.inode_id;
    if (inode_id == 0 || inode_id > m_sb.total_inodes)
        return false;

    const uint64_t index = inode_id - 1;
    const uint64_t byte_off = index * 256ull;
    const uint64_t lba4k = m_sb.inode_table_start + (byte_off >> 12);
    const size_t off = (size_t)(byte_off & 0xFFFu);

    alignas(4096) uint8_t blk[4096];
    if (!m_dev.read_blocks_4k(lba4k, 1, blk, sizeof(blk), con))
        return false;

    sylph1fs::Inode *p = reinterpret_cast<sylph1fs::Inode *>(blk + off);
    *p = ino; // 256B コピー（packed前提）
    p->inode_crc32c = 0;
    p->inode_crc32c = crc32c_reflected(p, 252);

    return m_dev.write_blocks_4k(lba4k, 1, blk, sizeof(blk), true, BlockDevice::kVerifyAfterWrite, con);
}

bool Sylph1Mount::dir_remove_entry(uint64_t parent_inode_id, const char *name,
                                   uint16_t &type_out, uint64_t &child_ino_out, Console &con)
{
    type_out = 0;
    child_ino_out = 0;
    size_t nlen = 0;
    uint64_t hdr_idx = 0;
    sylph1fs::Inode parent{};
    alignas(4096) uint8_t hdrblk[4096];
    sylph1fs::DirHeader *hdr;
    if (!check_dir_crc(parent_inode_id, name, nlen, parent, hdr, hdrblk, hdr_idx, con))
        return false;

    // バケット選択
    const uint64_t h = fnv1a64(name, nlen, 0);
    const uint32_t b = (uint32_t)(h % hdr->bucket_count);
    uint32_t *buckets = reinterpret_cast<uint32_t *>(hdrblk + sizeof(sylph1fs::DirHeader));
    uint32_t slot = buckets[b];
    if (slot == sylph1fs::kBucketEmpty || slot == sylph1fs::kBucketEmbedded)
    {
        return false; // 未対応 or 無し
    }

    // スラブ鎖を辿って該当名を tombstone 化
    uint64_t slab_idx = slot;
    while (slab_idx != 0)
    {
        alignas(4096) uint8_t slab[4096];
        if (!read_data_block(slab_idx, slab, con))
            return false;

        uint32_t s_stored = 0;
        memcpy(&s_stored, slab + 4096 - 4, 4);
        if (s_stored != crc32c_reflected(slab, 4096 - 4))
        {
            con.println("Sylph1FS: slab CRC mismatch");
            return false;
        }

        sylph1fs::DirSlabHeader *sh = reinterpret_cast<sylph1fs::DirSlabHeader *>(slab);
        uint32_t used = sh->used_bytes;
        if (used < sizeof(sylph1fs::DirSlabHeader) || used > 4096 - 4)
        {
            con.println("Sylph1FS: slab used out-of-range");
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

            if (type2 != 0 && nlen2 == nlen && memcmp(slab + p + 12, name, nlen) == 0)
            {
                // tombstone 化：type を 0 に
                uint16_t z = 0;
                memcpy(slab + p + 2, &z, 2);
                type_out = type2;
                child_ino_out = ino2;

                // スラブの entry_count をデクリメント
                if (sh->entry_count > 0)
                    sh->entry_count -= 1;

                // in-block CRC → 書戻し（＋サイドカーCRC）
                uint32_t s_crc = crc32c_reflected(slab, 4096 - 4);
                memcpy(slab + 4096 - 4, &s_crc, 4);
                if (!write_block_with_sidecar_crc(slab_idx, slab, con))
                    return false;

                // DirHeader.entry_count もデクリメント
                if (hdr->entry_count > 0)
                    hdr->entry_count -= 1;
                uint32_t h_crc2 = crc32c_reflected(hdrblk, 4096 - 4);
                memcpy(hdrblk + 4096 - 4, &h_crc2, 4);
                if (!write_block_with_sidecar_crc(hdr_idx, hdrblk, con))
                    return false;

                return true;
            }

            const uint32_t adv = align_up_u32(need, 8);
            if (!adv)
                break;
            p += adv;
        }

        slab_idx = sh->next_block_rel;
    }
    return false; // 見つからず
}

bool Sylph1Mount::is_dir_empty(uint64_t dir_inode_id, Console &con)
{
    sylph1fs::Inode ino{};
    if (!read_inode(dir_inode_id, ino, con))
        return false;
    if (ino.dir_format != 1 || ino.dir_header_block >= m_sb.data_area_blocks)
        return false;

    alignas(4096) uint8_t hdrblk[4096];
    if (!read_data_block(ino.dir_header_block, hdrblk, con))
        return false;
    uint32_t stored = 0;
    memcpy(&stored, hdrblk + 4096 - 4, 4);
    if (stored != crc32c_reflected(hdrblk, 4096 - 4))
        return false;

    const sylph1fs::DirHeader *hdr = reinterpret_cast<const sylph1fs::DirHeader *>(hdrblk);
    if (hdr->magic != sylph1fs::kDirMagic || hdr->version != 1)
        return false;

    // ヘッダの entry_count が 2 なら候補（実体 "." と ".." のみの想定）
    if (hdr->entry_count != 2)
        return false;

    // 念のため中身も確認：type!=0 の有効エントリが "." と ".." の2つだけか
    const uint32_t *buckets = reinterpret_cast<const uint32_t *>(hdrblk + sizeof(sylph1fs::DirHeader));
    uint32_t live = 0;

    for (uint32_t b = 0; b < hdr->bucket_count; ++b)
    {
        uint32_t slot = buckets[b];
        if (slot == sylph1fs::kBucketEmpty || slot == sylph1fs::kBucketEmbedded)
            continue;
        uint64_t slab_idx = slot;

        while (slab_idx != 0)
        {
            alignas(4096) uint8_t slab[4096];
            if (!read_data_block(slab_idx, slab, con))
                return false;
            uint32_t s_stored = 0;
            memcpy(&s_stored, slab + 4096 - 4, 4);
            if (s_stored != crc32c_reflected(slab, 4096 - 4))
                return false;

            const sylph1fs::DirSlabHeader *sh = reinterpret_cast<const sylph1fs::DirSlabHeader *>(slab);
            uint32_t used = sh->used_bytes;
            uint32_t p = sizeof(sylph1fs::DirSlabHeader);

            for (uint32_t i = 0; i < sh->entry_count && p + 12 <= used; ++i)
            {
                uint16_t nlen = 0, type = 0;
                uint64_t ino2 = 0;
                memcpy(&nlen, slab + p + 0, 2);
                memcpy(&type, slab + p + 2, 2);
                memcpy(&ino2, slab + p + 4, 8);
                const uint32_t need = (uint32_t)(12u + nlen);
                if (need > 4096 - 4 || p + need > used)
                    break;

                if (type != 0)
                {
                    // "." or ".." 判定（文字列比較）
                    bool is_dot = (nlen == 1 && slab[p + 12] == '.');
                    bool is_dotdot = (nlen == 2 && slab[p + 12] == '.' && slab[p + 13] == '.');
                    if (!is_dot && !is_dotdot)
                        return false;
                    ++live;
                }

                const uint32_t adv = align_up_u32(need, 8);
                if (!adv)
                    break;
                p += adv;
            }

            slab_idx = sh->next_block_rel;
        }
    }
    return (live == 2);
}

bool Sylph1Mount::unlink_path(const char *abs_path, Console &con)
{
    if (m_ro)
    {
        con.println("Sylph1FS: read-only mount");
        return false;
    }

    char base[256];
    size_t blen = 0;
    uint64_t parent = 0;
    if (!split_parent_basename(abs_path, parent, base, blen, con))
        return false;

    uint16_t ty = 0;
    uint64_t child = 0;
    if (!dir_remove_entry(parent, base, ty, child, con))
    {
        con.println("Sylph1FS: unlink: entry not found");
        return false;
    }
    if (ty != sylph1fs::kDirEntTypeFile)
    {
        con.println("Sylph1FS: unlink: not a file");
        return false;
    }

    // 子 inode のリンクをデクリメント
    sylph1fs::Inode ino{};
    if (!read_inode(child, ino, con))
        return false;
    if (ino.links > 0)
        ino.links -= 1;

    if (ino.links == 0)
    {
        // データ領域を解放し、inode を空に
        if (!free_file_storage(ino, 0, con)) // オフセット0から全てを解放
            return false;
        if (!write_inode(ino, con))
            return false;

        // 最後に Inode bitmap を free
        if (!set_inode_bitmap(child, /*used=*/false, con))
            return false;

        con.printf("Sylph1FS: unlinked and freed '%s' (ino=%llu)\n",
                   base, (unsigned long long)child);
        return true;
    }
    else
    {
        // 参照が残る場合は inode だけ更新
        if (!write_inode(ino, con))
            return false;
        con.printf("Sylph1FS: unlinked '%s' (ino=%llu, links=%u)\n",
                   base, (unsigned long long)child, (unsigned)ino.links);
        return true;
    }
}

bool Sylph1Mount::rmdir_path(const char *abs_path, Console &con)
{
    if (m_ro)
    {
        con.println("Sylph1FS: read-only mount");
        return false;
    }

    char base[256];
    size_t blen = 0;
    uint64_t parent = 0;
    if (!split_parent_basename(abs_path, parent, base, blen, con))
        return false;

    uint64_t child = 0;
    uint16_t ty = 0;
    if (!lookup_in_dir(parent, base, child, ty, con))
    {
        con.println("Sylph1FS: rmdir: entry not found");
        return false;
    }
    if (ty != sylph1fs::kDirEntTypeDir)
    {
        con.println("Sylph1FS: rmdir: not a directory");
        return false;
    }
    if (!is_dir_empty(child, con))
    {
        con.println("Sylph1FS: rmdir: directory not empty");
        return false;
    }

    // 親から tombstone 化（header.entry_count 減算も内部で行われる）
    uint16_t removed_ty = 0;
    uint64_t removed_ino = 0;
    if (!dir_remove_entry(parent, base, removed_ty, removed_ino, con))
        return false;

    // 親リンクを -1
    sylph1fs::Inode pin{};
    if (!read_inode(parent, pin, con))
        return false;
    if (pin.links > 0)
        pin.links -= 1;
    if (!write_inode(pin, con))
        return false;

    // 子ディレクトリの実体（ヘッダ/スラブ）を解放して inode を空に
    if (!free_dir_storage(child, con))
        return false;

    // Inode bitmap を解放
    if (!set_inode_bitmap(child, /*used=*/false, con))
        return false;

    con.printf("Sylph1FS: rmdir and freed '%s' (ino=%llu)\n",
               base, (unsigned long long)child);
    return true;
}

bool Sylph1Mount::write_path(const char *abs_path, const void *vbuf, uint64_t len, uint64_t off, Console &con)
{
    if (m_ro || !abs_path || !vbuf)
        return false;
    const uint8_t *buf = reinterpret_cast<const uint8_t *>(vbuf);
    if (len == 0)
        return true;

    uint64_t ino_id = 0;
    uint16_t ty = 0;
    if (!resolve_path_inode(abs_path, ino_id, ty, con))
        return false;
    if (ty != sylph1fs::kDirEntTypeFile)
    {
        con.println("Sylph1FS: write_path: not a file");
        return false;
    }
    return pwrite_file(ino_id, vbuf, off, len, con);
}

bool Sylph1Mount::read_path(const char *abs_path, void *vbuf, uint64_t len, uint64_t off, Console &con)
{
    if (!abs_path || !vbuf)
        return false;
    uint8_t *buf = reinterpret_cast<uint8_t *>(vbuf);
    if (len == 0)
        return true;

    uint64_t ino_id = 0;
    uint16_t ty = 0;
    if (!resolve_path_inode(abs_path, ino_id, ty, con))
        return false;
    if (ty != sylph1fs::kDirEntTypeFile)
    {
        con.println("Sylph1FS: read_path: not a file");
        return false;
    }

    sylph1fs::Inode ino{};
    if (!read_inode(ino_id, ino, con))
        return false;

    // ファイルサイズ超えは未対応（MVP）
    if (off + len > ino.size_bytes)
    {
        con.println("Sylph1FS: read beyond EOF");
        return false;
    }

    const uint64_t first_blk = off / 4096;
    const uint64_t last_blk = (off + len - 1) / 4096;
    size_t buf_off = 0;

    alignas(4096) uint8_t tmp_blk[4096];

    for (uint64_t fb = first_blk; fb <= last_blk; ++fb)
    {
        if (!pread_file_block(ino, fb, tmp_blk, con))
        {
            return false;
        }
        const size_t start_in_block = (fb == first_blk) ? (size_t)(off % 4096) : 0;
        const size_t end_in_block = (fb == last_blk) ? (size_t)((off + len - 1) % 4096) + 1 : 4096;
        const size_t bytes_to_copy = end_in_block - start_in_block;

        memcpy(buf + buf_off, tmp_blk + start_in_block, bytes_to_copy);
        buf_off += bytes_to_copy;
    }
    return true;
}

bool Sylph1Mount::truncate_path(const char *abs_path, uint64_t new_size, Console &con)
{
    if (m_ro)
    {
        con.println("Sylph1FS: read-only mount");
        return false;
    }
    if (!abs_path)
        return false;
    if (sx_mod(new_size, 4096) != 0)
    {
        con.println("Sylph1FS: truncate requires 4KiB-aligned size");
        return false;
    }

    uint64_t ino_id = 0;
    uint16_t ty = 0;
    if (!resolve_path_inode(abs_path, ino_id, ty, con))
        return false;
    if (ty != sylph1fs::kDirEntTypeFile)
    {
        con.println("Sylph1FS: truncate: not a file");
        return false;
    }

    sylph1fs::Inode ino{};
    if (!read_inode(ino_id, ino, con))
        return false;

    const uint64_t old_size = ino.size_bytes;

    if (new_size == old_size)
        return true;

    if (new_size > old_size)
    {
        // === extend ===
        const uint64_t cur_blocks = (old_size + 4095) / 4096;
        const uint64_t new_blocks = (new_size + 4095) / 4096;
        if (new_blocks > cur_blocks)
        {
            if (!allocate_file_blocks_and_attach(ino, new_blocks - cur_blocks, con))
            {
                con.println("Sylph1FS: extend allocate failed");
                return false;
            }
            // 新しく割り当てた領域をゼロフィルし、CRCも更新する
            PmmVec<sylph1fs::Extent> new_extents;
            if (!load_all_extents(ino, new_extents, con))
                return false;

            alignas(4096) uint8_t zero_block[4096];
            memset(zero_block, 0, sizeof(zero_block));

            uint64_t block_offset = 0;
            for (const auto &e : new_extents)
            {
                // エクステント内の各ブロックを走査
                for (uint64_t i = 0; i < e.length_blocks; ++i)
                {
                    // このブロックが新しく割り当てられたものか判定
                    if (block_offset + i >= cur_blocks)
                    {
                        uint64_t data_idx = e.start_block_rel + i;
                        // ゼロで埋めたブロックを書き込み（CRCも同時に更新）
                        if (!write_block_with_sidecar_crc(data_idx, zero_block, con))
                        {
                            con.println("Sylph1FS: truncate failed to zero-fill new block");
                            return false;
                        }
                    }
                }
                block_offset += e.length_blocks;
            }
        }
    }
    else
    {
        // === shrink ===
        uint64_t ofb_to_free = 0;
        if ((old_size + 4095) / 4096 > 4 && (new_size + 4095) / 4096 <= 4)
        {
            ofb_to_free = ino.overflow_extents_block;
        }
        // 1. 不要なデータブロックとCRCを解放
        if (!free_file_storage(ino, new_size, con))
        {
            con.println("Sylph1FS: shrink free_file_storage failed");
            return false;
        }
        // 2. エクステントリストを再構築する
        PmmVec<sylph1fs::Extent> all_extents;
        if (!load_all_extents(ino, all_extents, con))
            return false;
        PmmVec<sylph1fs::Extent> new_extents;
        uint64_t blocks_to_keep = (new_size + 4095) / 4096;
        uint64_t blocks_kept = 0;
        for (const auto &e : all_extents)
        {
            if (blocks_kept >= blocks_to_keep)
                break;
            uint64_t keep_len = e.length_blocks;
            if (blocks_kept + keep_len > blocks_to_keep)
            {
                keep_len = blocks_to_keep - blocks_kept;
            }
            sylph1fs::Extent new_e = {e.start_block_rel, (uint32_t)keep_len, 0};
            new_extents.push_back(new_e);
            blocks_kept += keep_len;
        }
        // 新しいエクステントリストをinodeに書き戻す
        ino.extent_count = 0;
        memset(ino.extents_inline, 0, sizeof(ino.extents_inline));
        ino.overflow_extents_block = 0; // まずクリア

        uint64_t ofb_idx = 0;
        if (ofb_to_free != 0 && new_extents.size() > 4)
        { // OFBが必要なら再利用
            ofb_idx = ofb_to_free;
            ino.overflow_extents_block = ofb_idx;
        }

        for (size_t i = 0; i < new_extents.size(); ++i)
        {
            if (i < 4)
            {
                ino.extents_inline[ino.extent_count++] = new_extents[i];
            }
            else
            {
                if (ofb_idx == 0)
                { // ensure_overflow_blockはinodeを書き込むのでここでは使わない
                    ofb_idx = ino.overflow_extents_block;
                }
                uint64_t tail = 0;
                if (!append_extent_to_overflow(ofb_idx, new_extents[i], tail, con))
                    return false;
            }
        }
    }
    // 最後にサイズを更新して inode を書き戻す
    ino.size_bytes = new_size;
    bool result = write_inode(ino, con);
    return result;
}

bool Sylph1Mount::free_file_storage(sylph1fs::Inode &ino, uint64_t start_offset_to_free, Console &con)
{
    PmmVec<sylph1fs::Extent> extents;
    if (!load_all_extents(ino, extents, con))
        return false;

    const uint64_t start_block_to_free = (start_offset_to_free + 4095) / 4096;
    uint64_t block_offset = 0;

    for (const auto &e : extents)
    {
        if (block_offset + e.length_blocks <= start_block_to_free)
        {
            // このエクステントは完全に保持される
            block_offset += e.length_blocks;
            continue;
        }

        uint64_t free_start_in_extent = (block_offset > start_block_to_free) ? 0 : start_block_to_free - block_offset;
        uint64_t free_count = e.length_blocks - free_start_in_extent;

        if (free_count > 0)
        {
            uint64_t data_idx_start = e.start_block_rel + free_start_in_extent;
            if (!clear_sidecar_crcs(data_idx_start, free_count, con))
                return false;
            if (!set_data_bitmap_range(data_idx_start, free_count, /*used=*/false, con))
                return false;
        }
        block_offset += e.length_blocks;
    }

    // inodeのエクステントリスト自体も再構築する必要があるが、
    // truncateでは後で再構築するため、ここではビットマップとCRCのクリアに専念する

    // 間接エクステントブロック自体も、不要になれば解放する
    if (start_block_to_free == 0 && ino.overflow_extents_block != 0)
    {
        if (!set_data_bitmap_range(ino.overflow_extents_block, 1, false, con))
            return false;
        ino.overflow_extents_block = 0;
    }

    return true;
}

bool Sylph1Mount::free_dir_storage(uint64_t dir_inode_id, Console &con)
{
    sylph1fs::Inode ino{};
    if (!read_inode(dir_inode_id, ino, con))
        return false;
    if (ino.dir_format != 1 || ino.dir_header_block >= m_sb.data_area_blocks)
    {
        con.println("Sylph1FS: free_dir_storage: not a hashed directory");
        return false;
    }

    // ヘッダブロックを読み、スラブ鎖を辿る
    alignas(4096) uint8_t hdr[4096];
    if (!read_data_block(ino.dir_header_block, hdr, con))
        return false;

    const sylph1fs::DirHeader *dh = reinterpret_cast<const sylph1fs::DirHeader *>(hdr);
    const uint32_t *buckets = reinterpret_cast<const uint32_t *>(hdr + sizeof(sylph1fs::DirHeader));

    // 各バケットのスラブ鎖を解放（空ディレクトリ想定："." と ".." しかないため、実質1〜数スラブ）
    for (uint32_t b = 0; b < dh->bucket_count; ++b)
    {
        uint32_t slot = buckets[b];
        if (slot == sylph1fs::kBucketEmpty)
            continue;
        if (slot == sylph1fs::kBucketEmbedded)
        {
            // v1 未対応：embedded 形態はスキップ（今は使用していない想定）
            continue;
        }
        // 外部スラブ鎖を FREE
        uint64_t slab = slot;
        while (slab != 0)
        {
            // 次ポインタを読む前にブロック取得
            alignas(4096) uint8_t blk[4096];
            if (!read_data_block(slab, blk, con))
                return false;
            const sylph1fs::DirSlabHeader *sh = reinterpret_cast<const sylph1fs::DirSlabHeader *>(blk);
            const uint64_t next = sh->next_block_rel;

            if (!set_data_bitmap_range(slab, 1, /*used=*/false, con))
                return false;
            slab = next;
        }
    }

    // ヘッダブロックも FREE
    if (!set_data_bitmap_range(ino.dir_header_block, 1, /*used=*/false, con))
        return false;

    // ディレクトリ inode 側のメタデータもクリーンアップ
    ino.dir_header_block = 0;
    ino.dir_format = 0;
    // extents にヘッダブロックを入れていた場合は念のため空に
    ino.extent_count = 0;
    for (uint32_t i = 0; i < 4; ++i)
    {
        ino.extents_inline[i].start_block_rel = 0;
        ino.extents_inline[i].length_blocks = 0;
    }
    ino.size_bytes = 0;

    return write_inode(ino, con);
}

bool Sylph1Mount::stat_path(const char *abs_path, SylphStat &st, Console &con)
{
    // 初期化
    st.type = 0;
    st.mode = 0;
    st.links = 0;
    st.size = 0;
    st.inode_id = 0;
    st.ctime = 0;
    st.mtime = 0;
    st.atime = 0;

    if (!abs_path)
        return false;

    uint64_t ino_id = 0;
    uint16_t ty = 0;
    if (!resolve_path_inode(abs_path, ino_id, ty, con))
    {
        con.printf("Sylph1FS: stat: resolve failed for '%s'\n", abs_path ? abs_path : "(null)");
        return false;
    }

    sylph1fs::Inode ino{};
    if (!read_inode(ino_id, ino, con))
    {
        con.printf("Sylph1FS: stat: failed to read inode #%llu\n", (unsigned long long)ino_id);
        return false;
    }

    st.type = ty; // 1=DIR, 2=FILE
    st.mode = (uint16_t)ino.mode;
    st.links = (uint32_t)ino.links;
    st.size = ino.size_bytes;
    st.inode_id = ino.inode_id;

    // いまは時刻未運用：0のまま。将来、ctime/mtime/atimeをinodeに追加後にセット。

    return true;
}

bool Sylph1Mount::load_all_extents(const sylph1fs::Inode &ino,
                                   PmmVec<sylph1fs::Extent> &out,
                                   Console &con)
{
    out.clear();
    // inline
    const uint16_t n = ino.extent_count;
    if (n > 0)
    {
        for (uint16_t i = 0; i < n && i < 4; ++i)
        {
            const auto &e = ino.extents_inline[i];
            if (e.length_blocks)
                out.push_back(e);
        }
    }
    // overflow
    uint64_t cur = ino.overflow_extents_block; // data area 相対 index
    while (cur != 0)
    {
        alignas(4096) uint8_t blk[4096];
        if (!read_data_block(cur, blk, con))
            return false;

        // in-block CRC は read_data_block() 側で sidecar と共に検証済み
        const auto *hdr = reinterpret_cast<const sylph1fs::ExtentOverflowHeader *>(blk);
        if (hdr->magic != sylph1fs::kExtOvMagic || hdr->version != 1)
        {
            con.println("Sylph1FS: extent overflow header invalid");
            return false;
        }
        // ヘッダ直後に Extent が並ぶ（4096-4 の範囲まで）
        const uint32_t cap_bytes = 4096 - 4 - sizeof(*hdr);
        const uint32_t cap_entries = cap_bytes / sizeof(sylph1fs::Extent);
        const uint32_t cnt = (hdr->entry_count <= cap_entries) ? hdr->entry_count : cap_entries;

        const auto *arr = reinterpret_cast<const sylph1fs::Extent *>(blk + sizeof(*hdr));
        for (uint32_t i = 0; i < cnt; ++i)
        {
            if (arr[i].length_blocks)
                out.push_back(arr[i]);
        }
        cur = hdr->next_block_rel;
    }
    return true;
}

bool Sylph1Mount::ensure_overflow_block(sylph1fs::Inode &ino,
                                        uint64_t &ofb_idx_out, Console &con)
{
    if (ino.overflow_extents_block != 0)
    {
        ofb_idx_out = ino.overflow_extents_block;
        return true;
    }
    // 新規確保
    uint64_t idx = 0;
    if (!alloc_data_blocks(1, idx, con))
        return false;

    alignas(4096) uint8_t blk[4096];
    memset(blk, 0, sizeof(blk));
    auto *hdr = reinterpret_cast<sylph1fs::ExtentOverflowHeader *>(blk);
    hdr->magic = sylph1fs::kExtOvMagic;
    hdr->version = 1;
    hdr->entry_count = 0;
    hdr->next_block_rel = 0;

    // in-block CRC を末尾へ
    uint32_t crc = crc32c_reflected(blk, 4096 - 4);
    memcpy(blk + 4096 - 4, &crc, 4);

    if (!write_block_with_sidecar_crc(idx, blk, con))
        return false;
    if (!set_data_bitmap_range(idx, 1, true, con))
        return false;

    // inode へ反映（CRC 更新）
    ino.overflow_extents_block = idx;
    return write_inode(ino, con);
}

bool Sylph1Mount::append_extent_to_overflow(
    uint64_t ofb_idx,
    const sylph1fs::Extent &e,
    uint64_t &tail_idx_out,
    Console &con)
{
    uint64_t cur = ofb_idx;
    for (;;)
    {
        alignas(4096) uint8_t blk[4096];
        if (!read_data_block(cur, blk, con))
            return false;

        auto *hdr = reinterpret_cast<sylph1fs::ExtentOverflowHeader *>(blk);
        const uint32_t cap = (uint32_t)((4096 - 4 - sizeof(*hdr)) / sizeof(sylph1fs::Extent));
        auto *arr = reinterpret_cast<sylph1fs::Extent *>(blk + sizeof(*hdr));

        if (hdr->entry_count < cap)
        {
            arr[hdr->entry_count] = e;
            hdr->entry_count++;

            uint32_t crc = crc32c_reflected(blk, 4096 - 4);
            memcpy(blk + 4096 - 4, &crc, 4);
            if (!write_block_with_sidecar_crc(cur, blk, con))
                return false;

            tail_idx_out = cur;
            return true;
        }

        if (hdr->next_block_rel != 0)
        {
            cur = hdr->next_block_rel;
            continue;
        }

        // 新しい OFB を確保して連結
        uint64_t new_idx = 0;
        if (!alloc_data_blocks(1, new_idx, con))
            return false;

        alignas(4096) uint8_t nblk[4096];
        memset(nblk, 0, sizeof(nblk));
        auto *nh = reinterpret_cast<sylph1fs::ExtentOverflowHeader *>(nblk);
        nh->magic = sylph1fs::kExtOvMagic;
        nh->version = 1;
        nh->entry_count = 0;
        nh->next_block_rel = 0;
        uint32_t ns_crc = crc32c_reflected(nblk, 4096 - 4);
        memcpy(nblk + 4096 - 4, &ns_crc, 4);
        if (!write_block_with_sidecar_crc(new_idx, nblk, con))
            return false;
        if (!set_data_bitmap_range(new_idx, 1, true, con))
            return false;

        // 旧ブロックに next を繋ぐ
        hdr->next_block_rel = new_idx;
        uint32_t crc2 = crc32c_reflected(blk, 4096 - 4);
        memcpy(blk + 4096 - 4, &crc2, 4);
        if (!write_block_with_sidecar_crc(cur, blk, con))
            return false;

        cur = new_idx;
        // 次ループで追記
    }
}

bool Sylph1Mount::allocate_file_blocks_and_attach(sylph1fs::Inode &ino,
                                                  uint64_t need_blocks,
                                                  Console &con)
{
    // 既存エクステントを取得（併合のため）
    PmmVec<sylph1fs::Extent> cur;
    if (!load_all_extents(ino, cur, con))
        return false;

    // ひたすら確保して繋ぐ（first-fit 連続ラン API は既存の alloc_data_blocks()）
    while (need_blocks > 0)
    {
        const uint32_t chunk = (need_blocks > 0xFFFFFFFFull) ? 0xFFFFFFFFu
                                                             : (uint32_t)need_blocks;
        uint64_t start_idx = 0;
        if (!alloc_data_blocks(chunk, start_idx, con))
        {
            // 大きすぎて連続が取れないときは、chunk を半分にして試行（簡易分割）
            uint32_t try_chunk = (chunk > 1) ? (chunk / 2) : 1;
            bool ok = false;
            while (try_chunk >= 1)
            {
                if (alloc_data_blocks(try_chunk, start_idx, con))
                {
                    ok = true;
                    break;
                }
                if (try_chunk == 1)
                    break;
                try_chunk /= 2;
            }
            if (!ok)
            {
                con.println("Sylph1FS: no space for file growth");
                return false;
            }
            // 実際に確保できたサイズへ
            // (start_idx は上で更新済み)
            // try_chunk を chunk に読み替え
        }
        // ここに来たら start_idx, chunk が有効
        sylph1fs::Extent e{};
        e.start_block_rel = start_idx;
        e.length_blocks = chunk;

        // 連続なら併合
        if (!try_coalesce_last(cur, e))
        {
            cur.push_back(e);
        }

        // ビットマップ確定
        if (!set_data_bitmap_range(start_idx, chunk, /*used*/ true, con))
            return false;

        need_blocks -= chunk;
    }

    // inode へ反映：まず inline に 4 本詰め、溢れは OFB へ
    // inline をリビルド
    sylph1fs::Inode tmp = ino;
    memset(tmp.extents_inline, 0, sizeof(tmp.extents_inline));
    tmp.extent_count = 0;

    // ▼▼▼ この部分を修正 ▼▼▼
    tmp.overflow_extents_block = 0;

    uint64_t ofb_idx = 0;
    if (cur.size() > 4)
    { // もしOFBが必要なら
        if (!ensure_overflow_block(tmp, ofb_idx, con))
            return false;
    }

    for (const auto &e : cur)
    {
        if (tmp.extent_count < 4)
        {
            tmp.extents_inline[tmp.extent_count++] = e;
        }
        else
        {
            uint64_t tail = 0;
            if (!append_extent_to_overflow(ofb_idx, e, tail, con))
                return false;
        }
    }
    // ▲▲▲ 修正ここまで ▲▲▲

    // inode の size_bytes は呼び出し側で更新してから CRC・書き戻し
    ino = tmp;
    return true;
}

bool Sylph1Mount::pread_file_block(const sylph1fs::Inode &ino,
                                   uint64_t file_block_idx, // 4KiBブロック単位
                                   void *out4096,
                                   Console &con)
{
    PmmVec<sylph1fs::Extent> es;
    if (!load_all_extents(ino, es, con))
        return false;

    uint64_t acc = 0;
    for (const auto &e : es)
    {
        if (file_block_idx < acc + e.length_blocks)
        {
            const uint64_t rel = file_block_idx - acc;
            const uint64_t data_idx = e.start_block_rel + rel;
            return read_data_block(data_idx, out4096, con);
        }
        acc += e.length_blocks;
    }
    con.println("Sylph1FS: pread beyond EOF extents");
    return false;
}

bool Sylph1Mount::pwrite_file(const uint64_t inode_id,
                              const void *src, uint64_t off, uint64_t len, Console &con)
{
    if (m_ro)
    {
        con.println("Sylph1FS: read-only mount");
        return false;
    }

    sylph1fs::Inode ino{};
    if (!read_inode(inode_id, ino, con))
        return false;

    const uint64_t original_size = ino.size_bytes;

    // 必要ブロック数を計算し、不足分を確保
    const uint64_t end = off + len;
    const uint64_t need_bytes = (end > ino.size_bytes) ? (end - ino.size_bytes) : 0;
    if (need_bytes)
    {
        const uint64_t cur_blocks = (ino.size_bytes + 4095) >> 12;
        const uint64_t new_blocks = (end + 4095) >> 12;
        const uint64_t add = (new_blocks > cur_blocks) ? (new_blocks - cur_blocks) : 0;
        if (add)
        {
            if (!allocate_file_blocks_and_attach(ino, add, con))
                return false;
        }
    }

    // 書き込み（4KiB アラインに分割）: 既存の write_block_with_sidecar_crc を使用
    uint64_t written = 0;
    while (written < len)
    {
        const uint64_t abs_offset = off + written;
        const uint64_t fblk = abs_offset >> 12;
        const uint32_t inblk = (uint32_t)(abs_offset & 0xFFFu);
        const uint32_t chunk = (uint32_t)min<uint64_t>(len - written, 4096 - inblk);

        alignas(4096) uint8_t blk[4096];
        // 既存データを読む（部分更新）
        if (!pread_file_block(ino, fblk, blk, con))
        {
            // ▼▼▼ ここから修正 ▼▼▼
            // 読み込みに失敗した場合のハンドリング
            if (abs_offset < original_size)
            {
                // ファイルの既存領域の読み込み失敗は、データ破損を示す致命的エラー
                con.println("Sylph1FS: pwrite RMW failed due to unrecoverable read error on existing data!");
                return false;
            }
            // 新規拡張領域の読み込み失敗は許容し、バッファをゼロ埋めして続行
            memset(blk, 0, sizeof(blk));
            // ▲▲▲ ここまで修正 ▲▲▲
        }

        memcpy(blk + inblk, (const uint8_t *)src + written, chunk);

        // 論理→物理へ再マッピングして書込み
        PmmVec<sylph1fs::Extent> es;
        if (!load_all_extents(ino, es, con))
            return false;
        uint64_t acc = 0, data_idx = 0;
        bool found = false;
        for (const auto &e : es)
        {
            if (fblk < acc + e.length_blocks)
            {
                data_idx = e.start_block_rel + (fblk - acc);
                found = true;
                break;
            }
            acc += e.length_blocks;
        }
        if (!found)
        {
            con.println("Sylph1FS: mapping vanished");
            return false;
        }

        if (!write_block_with_sidecar_crc(data_idx, blk, con))
            return false;

        written += chunk;
    }

    // サイズ更新 + inode CRC
    if (end > ino.size_bytes)
        ino.size_bytes = end;
    return write_inode(ino, con);
}

bool Sylph1Mount::update_dotdot_entry(uint64_t dir_inode_id, uint64_t new_parent_inode_id, Console &con)
{
    sylph1fs::Inode dir_ino{};
    if (!read_inode(dir_inode_id, dir_ino, con))
        return false;

    // ディレクトリのヘッダブロックを読み込む
    alignas(4096) uint8_t hdrblk[4096];
    if (!read_data_block(dir_ino.dir_header_block, hdrblk, con))
        return false;

    const sylph1fs::DirHeader *hdr = reinterpret_cast<const sylph1fs::DirHeader *>(hdrblk);
    const uint32_t *buckets = reinterpret_cast<const uint32_t *>(hdrblk + sizeof(sylph1fs::DirHeader));

    // 全バケットを走査して ".." エントリを探す
    for (uint32_t b = 0; b < hdr->bucket_count; ++b)
    {
        uint64_t slab_idx = buckets[b];
        while (slab_idx != 0)
        {
            alignas(4096) uint8_t slab[4096];
            if (!read_data_block(slab_idx, slab, con))
                return false;

            sylph1fs::DirSlabHeader *sh = reinterpret_cast<sylph1fs::DirSlabHeader *>(slab);
            uint32_t p = sizeof(sylph1fs::DirSlabHeader);

            for (uint32_t i = 0; i < sh->entry_count; ++i)
            {
                uint16_t nlen = 0;
                memcpy(&nlen, slab + p, 2);

                // ".." を見つけたら inode 番号を上書き
                if (nlen == 2 && slab[p + 12] == '.' && slab[p + 13] == '.')
                {
                    memcpy(slab + p + 4, &new_parent_inode_id, 8);

                    // 変更したスラブを書き戻す（CRC含む）
                    return write_block_with_sidecar_crc(slab_idx, slab, con);
                }

                const uint32_t adv = align_up_u32((uint32_t)(12 + nlen), 8);
                if (adv == 0)
                    break;
                p += adv;
            }
            slab_idx = sh->next_block_rel;
        }
    }
    return false; // ".." が見つからなかった (通常ありえない)
}

bool Sylph1Mount::rename_path(const char *old_path, const char *new_path, Console &con)
{
    if (m_ro)
    {
        con.println("Sylph1FS: read-only mount");
        return false;
    }
    if (!old_path || !new_path || strcmp(old_path, new_path) == 0)
    {
        return false;
    }

    // 1. パスの解析と事前チェック
    uint64_t src_ino_id = 0;
    uint16_t src_type = 0;
    if (!resolve_path_inode(old_path, src_ino_id, src_type, con))
    {
        con.printf("Sylph1FS: rename: source '%s' not found\n", old_path);
        return false;
    }

    char old_base[256];
    size_t old_base_len = 0;
    uint64_t old_parent_ino_id = 0;
    if (!split_parent_basename(old_path, old_parent_ino_id, old_base, old_base_len, con))
        return false;

    char new_base[256];
    size_t new_base_len = 0;
    uint64_t new_parent_ino_id = 0;
    if (!split_parent_basename(new_path, new_parent_ino_id, new_base, new_base_len, con))
        return false;

    uint64_t dest_ino_id = 0;
    uint16_t dest_type = 0;
    if (resolve_path_inode(new_path, dest_ino_id, dest_type, con))
    {
        con.printf("Sylph1FS: rename: destination '%s' already exists\n", new_path);
        return false;
    }

    // ディレクトリを自身の子孫に移動させることはできない (簡易チェック)
    if (src_type == sylph1fs::kDirEntTypeDir && new_parent_ino_id == src_ino_id)
    {
        con.println("Sylph1FS: rename: cannot move a directory into itself");
        return false;
    }
    // (より完全なチェックは、new_parent からルートまで遡って src_ino がいないか確認する必要がある)

    // 2. 新しい場所にリンクを作成
    if (!dir_add_entry(new_parent_ino_id, new_base, src_type, src_ino_id, con))
    {
        con.println("Sylph1FS: rename: failed to create new link");
        return false;
    }

    // 3. ディレクトリ移動時の特別処理
    if (src_type == sylph1fs::kDirEntTypeDir && old_parent_ino_id != new_parent_ino_id)
    {
        // 古い親のリンクカウントを -1
        sylph1fs::Inode old_parent_ino{};
        if (read_inode(old_parent_ino_id, old_parent_ino, con))
        {
            if (old_parent_ino.links > 0)
                old_parent_ino.links--;
            write_inode(old_parent_ino, con);
        }

        // 新しい親のリンクカウントを +1
        sylph1fs::Inode new_parent_ino{};
        if (read_inode(new_parent_ino_id, new_parent_ino, con))
        {
            new_parent_ino.links++;
            write_inode(new_parent_ino, con);
        }

        // 移動したディレクトリ自身の ".." エントリを更新
        if (!update_dotdot_entry(src_ino_id, new_parent_ino_id, con))
        {
            con.println("Sylph1FS: rename: failed to update '..' entry");
            // ここで失敗した場合、手動での復旧が必要になる可能性がある
        }
    }

    // 4. 古い場所のリンクを削除
    uint16_t removed_type = 0;
    uint64_t removed_ino = 0;
    if (!dir_remove_entry(old_parent_ino_id, old_base, removed_type, removed_ino, con))
    {
        con.println("Sylph1FS: rename: failed to remove old link (INCONSISTENT STATE)");
        return false;
    }

    con.printf("Sylph1FS: renamed '%s' to '%s'\n", old_path, new_path);
    return true;
}

bool Sylph1Mount::clear_sidecar_crcs(uint64_t start_data_idx, uint32_t count, Console &con)
{
    if (count == 0)
        return true;

    alignas(4096) uint8_t crc_blk[4096];
    uint64_t current_crc_lba = 0;

    for (uint32_t i = 0; i < count; ++i)
    {
        uint64_t data_idx = start_data_idx + i;
        uint64_t crc_lba = 0;
        size_t crc_off = 0;
        if (!map_crc_entry(data_idx, crc_lba, crc_off, con))
            return false;

        // CRCブロックが変わった最初のタイミングで読み込む
        if (crc_lba != current_crc_lba)
        {
            if (current_crc_lba != 0)
            { // 前のブロックに変更があれば書き戻す
                if (!m_dev.write_blocks_4k(current_crc_lba, 1, crc_blk, 4096, true, BlockDevice::kVerifyAfterWrite, con))
                    return false;
            }
            if (!m_dev.read_blocks_4k(crc_lba, 1, crc_blk, 4096, con))
                return false;
            current_crc_lba = crc_lba;
        }

        // メモリ上でCRCエントリをゼロにする
        uint32_t zero = 0;
        memcpy(crc_blk + crc_off, &zero, sizeof(zero));
    }

    // 最後に残ったブロックを書き戻す
    if (current_crc_lba != 0)
    {
        if (!m_dev.write_blocks_4k(current_crc_lba, 1, crc_blk, 4096, true, BlockDevice::kVerifyAfterWrite, con))
            return false;
    }

    return true;
}