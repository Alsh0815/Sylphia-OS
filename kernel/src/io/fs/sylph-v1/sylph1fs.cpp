#include "../../../../include/std/algorithm.hpp"
#include "../../../../include/std/cstring.hpp"
#include "../../../console.hpp"
#include "../../../kernel_runtime.hpp"
#include "sylph1fs.hpp"

Sylph1FS::Sylph1FS(BlockDevice &dev, Console &con)
    : m_dev(dev), m_con(con) {}

uint32_t Sylph1FS::crc32c(const void *data, size_t len)
{
    // 多項式 0x1EDC6F41、ビット反転・LSBファースト版は 0x82F63B78
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

// --- 4KiBゼロブロックを blocks 回連続書込み ---
bool Sylph1FS::write_zeros(uint64_t start_lba4k, uint64_t blocks)
{
    alignas(4096) uint8_t zero[4096];
    memset(zero, 0, sizeof(zero));
    for (uint64_t i = 0; i < blocks; ++i)
    {
        if (!m_dev.write_blocks_4k(start_lba4k + i, 1, zero, sizeof(zero),
                                   /*fua*/ true, BlockDevice::kVerifyAfterWrite, m_con))
        {
            m_con.printf("mkfs: write_zeros failed at LBA=%llu\n", (unsigned long long)(start_lba4k + i));
            return false;
        }
    }
    return true;
}

// --- 4KiBをFUA+Verifyで書く ---
bool Sylph1FS::write_and_verify(uint64_t lba4k, const void *buf4096)
{
    return m_dev.write_blocks_4k(lba4k, 1, buf4096, 4096,
                                 /*fua*/ true, BlockDevice::kVerifyAfterWrite, m_con);
}

// --- レイアウト計算（2段収束） ---
bool Sylph1FS::compute_layout(Layout &L, uint64_t total_inodes_hint)
{
    memset(&L, 0, sizeof(L));
    L.total_blocks = m_dev.logical_block_count_4k();
    if (L.total_blocks < 8)
    { // あまりにも小さい場合は不可
        m_con.println("mkfs: too few blocks");
        return false;
    }

    // inode数ヒューリスティク：1 inode / 64KiB
    const uint64_t default_inodes = max<uint64_t>(1, (L.total_blocks * 4096ull + 65535ull) / 65536ull);
    L.total_inodes = (total_inodes_hint ? total_inodes_hint : default_inodes);

    const uint64_t INODE_SIZE = 256;
    const uint64_t BITS_PER_BM_BLOCK = 4096ull * 8ull; // 32768
    const uint64_t DATA_PER_CRC_BLOCK = 1024ull;       // 1CRCブロック=1024データブロック

    const uint64_t bm_inode_blocks = (L.total_inodes + BITS_PER_BM_BLOCK - 1) / BITS_PER_BM_BLOCK;
    const uint64_t inode_table_blocks = ((L.total_inodes * INODE_SIZE) + 4095ull) / 4096ull;

    // 初期推定
    uint64_t overhead0 = 1 + bm_inode_blocks + inode_table_blocks + 1;
    if (overhead0 >= L.total_blocks)
    {
        m_con.println("mkfs: overhead exceeds total blocks");
        return false;
    }
    uint64_t data_guess = L.total_blocks - overhead0;

    auto derive = [&](uint64_t data_blocks, uint64_t &bm_data_blocks, uint64_t &crc_area_blocks)
    {
        bm_data_blocks = (data_blocks + BITS_PER_BM_BLOCK - 1) / BITS_PER_BM_BLOCK;
        crc_area_blocks = (data_blocks + DATA_PER_CRC_BLOCK - 1) / DATA_PER_CRC_BLOCK;
    };

    uint64_t bm_data_blocks = 0, crc_area_blocks = 0;
    derive(data_guess, bm_data_blocks, crc_area_blocks);

    // 収束ステップ（通常1～2回で安定）
    for (int iter = 0; iter < 4; ++iter)
    {
        uint64_t overhead = 1 + bm_inode_blocks + bm_data_blocks + inode_table_blocks + crc_area_blocks + 1;
        if (overhead >= L.total_blocks)
        {
            m_con.println("mkfs: overhead exceeds total blocks (iter)");
            return false;
        }
        uint64_t data_blocks = L.total_blocks - overhead;

        uint64_t bm2 = 0, crc2 = 0;
        derive(data_blocks, bm2, crc2);
        if (bm2 == bm_data_blocks && crc2 == crc_area_blocks)
        {
            // 確定
            L.sb_primary_lba4k = 0;
            uint64_t cur = 1;

            L.bm_inode_start = cur;
            L.bm_inode_blocks = bm_inode_blocks;
            cur += bm_inode_blocks;
            L.bm_data_start = cur;
            L.bm_data_blocks = bm_data_blocks;
            cur += bm_data_blocks;
            L.inode_table_start = cur;
            L.inode_table_blocks = inode_table_blocks;
            cur += inode_table_blocks;
            L.data_area_start = cur;
            L.data_area_blocks = data_blocks;
            cur += data_blocks;
            L.crc_area_start = cur;
            L.crc_area_blocks = crc_area_blocks;
            cur += crc_area_blocks;

            L.sb_backup_lba4k = L.total_blocks - 1;
            if (cur > L.sb_backup_lba4k)
            {
                m_con.println("mkfs: layout overflow at finalize");
                return false;
            }
            return true;
        }
        bm_data_blocks = bm2;
        crc_area_blocks = crc2;
    }

    m_con.println("mkfs: layout did not converge");
    return false;
}

// --- Superblock 初期書込み（INCOMPLETE=1, CLEAN=0） ---
bool Sylph1FS::write_superblock_initial(const Layout &L, const MkfsOptions &opt)
{
    // あなたの sylphfs_structs.hpp に定義された on-disk Superblock 構造を想定
    sylph1fs::Superblock sb{};
    memset(&sb, 0, sizeof(sb));

    sb.magic = 0x53494C46u; // 'S''Y''L''F'
    sb.version = opt.version;
    sb.minor_version = opt.minor_version;
    sb.block_size_log2 = 12;
    sb.csum_kind = 1; // CRC32C
    sb.sb_flags = 0;
    // bit: CLEAN(1)=0, INCOMPLETE(2)=1
    sb.sb_flags |= (1u << 1);

    sb.features_compat = opt.features_compat;
    sb.features_ro_compat = opt.features_ro_compat;
    sb.features_incompat = opt.features_incompat;

    // uuid/label
    if (opt.uuid16)
        memcpy(sb.uuid, opt.uuid16, 16);
    if (opt.label)
    {
        size_t n = min<size_t>(strlen(opt.label), sizeof(sb.label));
        memcpy(sb.label, opt.label, n);
        if (n < sizeof(sb.label))
            sb.label[n] = '\0';
    }

    sb.total_blocks = L.total_blocks;
    sb.total_inodes = L.total_inodes;
    sb.inode_size = 256;

    sb.root_inode = 1;
    sb.sb_primary_lba4k = L.sb_primary_lba4k;
    sb.sb_backup_lba4k = L.sb_backup_lba4k;

    sb.bm_inode_start = L.bm_inode_start;
    sb.bm_inode_blocks = L.bm_inode_blocks;
    sb.bm_data_start = L.bm_data_start;
    sb.bm_data_blocks = L.bm_data_blocks;
    sb.inode_table_start = L.inode_table_start;
    sb.inode_table_blocks = L.inode_table_blocks;
    sb.data_area_start = L.data_area_start;
    sb.data_area_blocks = L.data_area_blocks;
    sb.crc_area_start = L.crc_area_start;
    sb.crc_area_blocks = L.crc_area_blocks;

    // ヒント/シークレットは0で開始（将来：ランダム生成）
    // dirhash_secret も0（マウント後に設定/更新可）

    // CRC計算（先頭4092B）
    sb.sb_crc32c = 0;
    sb.sb_crc32c = crc32c(&sb, 4092);

    // Primary SB 書込み+検証
    if (!write_and_verify(L.sb_primary_lba4k, &sb))
        return false;

    return true;
}

// --- Superblock 最終化（CLEAN=1, INCOMPLETE=0） ---
bool Sylph1FS::finalize_superblocks(const Layout &L, const MkfsOptions &opt)
{
    sylph1fs::Superblock sb{};
    // Primary を読み戻してから flags だけ更新するのが堅いが、
    // ここでは同じ値を再生成して flags を反転させて書く。
    if (!write_superblock_initial(L, opt))
        return false; // 先に Primary を最新に（INCOMPLETE=1）

    // Primary を再構築し CLEAN=1 に変更
    // 上の関数で書いた内容をもう一度作る（冪等）
    // ここではローカルに再構築
    memset(&sb, 0, sizeof(sb));
    sb.magic = 0x53494C46u;
    sb.version = opt.version;
    sb.minor_version = opt.minor_version;
    sb.block_size_log2 = 12;
    sb.csum_kind = 1;
    sb.sb_flags = 0;          // CLEAN=1, INCOMPLETE=0 にしたいので0から
    sb.sb_flags |= (1u << 0); // CLEAN
    sb.features_compat = opt.features_compat;
    sb.features_ro_compat = opt.features_ro_compat;
    sb.features_incompat = opt.features_incompat;

    if (opt.uuid16)
        memcpy(sb.uuid, opt.uuid16, 16);
    if (opt.label)
    {
        size_t n = min<size_t>(strlen(opt.label), sizeof(sb.label));
        memcpy(sb.label, opt.label, n);
        if (n < sizeof(sb.label))
            sb.label[n] = '\0';
    }

    sb.total_blocks = L.total_blocks;
    sb.total_inodes = L.total_inodes;
    sb.inode_size = 256;

    sb.root_inode = 1;
    sb.sb_primary_lba4k = L.sb_primary_lba4k;
    sb.sb_backup_lba4k = L.sb_backup_lba4k;

    sb.bm_inode_start = L.bm_inode_start;
    sb.bm_inode_blocks = L.bm_inode_blocks;
    sb.bm_data_start = L.bm_data_start;
    sb.bm_data_blocks = L.bm_data_blocks;
    sb.inode_table_start = L.inode_table_start;
    sb.inode_table_blocks = L.inode_table_blocks;
    sb.data_area_start = L.data_area_start;
    sb.data_area_blocks = L.data_area_blocks;
    sb.crc_area_start = L.crc_area_start;
    sb.crc_area_blocks = L.crc_area_blocks;

    sb.sb_crc32c = 0;
    sb.sb_crc32c = crc32c(&sb, 4092);

    // Primary SB 上書き → Backup SB へも書く
    if (!write_and_verify(L.sb_primary_lba4k, &sb))
        return false;
    if (!write_and_verify(L.sb_backup_lba4k, &sb))
        return false;

    return true;
}

// --- メタ領域のクリア（ゼロ埋め） ---
bool Sylph1FS::clear_meta_areas(const Layout &L)
{
    if (!write_zeros(L.bm_inode_start, L.bm_inode_blocks))
        return false;
    if (!write_zeros(L.bm_data_start, L.bm_data_blocks))
        return false;
    if (!write_zeros(L.inode_table_start, L.inode_table_blocks))
        return false;
    if (!write_zeros(L.crc_area_start, L.crc_area_blocks))
        return false;
    return true;
}

// --- ルートinode(#1) 初期化（最小：サイズ0 / extentsなし / ディレクトリ） ---
bool Sylph1FS::init_root_inode(const Layout &L, const MkfsOptions &opt)
{
    // Inode#1 の位置計算
    const uint64_t index = 0; // inode_id=1 -> index0
    const uint64_t byte_off = index * 256ull;
    const uint64_t lba4k = L.inode_table_start + (byte_off / 4096ull);
    const size_t off = (size_t)(byte_off % 4096ull);

    alignas(4096) uint8_t buf[4096];
    if (!m_dev.read_blocks_4k(lba4k, 1, buf, sizeof(buf), m_con))
        return false;

    // 256Bの領域をゼロ化してから Inode を構築
    memset(buf + off, 0, 256);
    sylph1fs::Inode *ino = reinterpret_cast<sylph1fs::Inode *>(buf + off);

    ino->inode_id = 1;
    ino->mode = 0x4000 | 0755; // DIR | 0755
    ino->links = 1;
    ino->uid = 0;
    ino->gid = 0;
    ino->flags = 0;
    ino->size_bytes = 0; // ここでは最小化（DirHeader等は後で拡張）
    ino->atime = ino->mtime = ino->ctime = 0;
    ino->extent_count = 0;
    ino->overflow_extents_block = 0;
    ino->xattr_block = 0;
    ino->dir_format = 1;       // ハッシュ化ディレクトリを既定に
    ino->dir_header_block = 0; // まだ未割当（空ディレクトリとして扱う）
    // 末尾CRC
    ino->inode_crc32c = 0;
    ino->inode_crc32c = crc32c(ino, 252);

    // ブロックへ書戻し
    if (!write_and_verify(lba4k, buf))
        return false;

    // Inode bitmap で #1 を使用中にする（LSB-first: bit0が inode#1）
    // 対象バイトの読み→bit set→書戻し
    const uint64_t bm_byte_index = 0; // inode#1 は byte0 bit0
    const uint64_t bm_lba = L.bm_inode_start + (bm_byte_index / 4096ull);
    const size_t bm_off = (size_t)(bm_byte_index % 4096ull);

    alignas(4096) uint8_t bm_buf[4096];
    if (!m_dev.read_blocks_4k(bm_lba, 1, bm_buf, sizeof(bm_buf), m_con))
        return false;
    bm_buf[bm_off] |= 0x01; // bit0 = 1
    if (!write_and_verify(bm_lba, bm_buf))
        return false;

    return true;
}

// --- mkfs 本体 ---
FsStatus Sylph1FS::mkfs(const MkfsOptions &opt)
{
    // 1) レイアウト計算
    Layout L{};
    if (!compute_layout(L, opt.total_inodes))
    {
        return FsStatus::InvalidArg;
    }

    // 2) Primary SB (INCOMPLETE=1) を先に出す
    if (!write_superblock_initial(L, opt))
    {
        m_con.println("mkfs: write_superblock_initial failed");
        return FsStatus::IoError;
    }

    // 3) メタ領域をクリア（ビットマップ/テーブル/CRC）
    if (!clear_meta_areas(L))
    {
        m_con.println("mkfs: clear_meta_areas failed");
        return FsStatus::IoError;
    }

    // 4) ルートinode(#1) 最小作成（空ディレクトリとして）
    if (!init_root_inode(L, opt))
    {
        m_con.println("mkfs: init_root_inode failed");
        return FsStatus::IoError;
    }

    // 5) 最終 Superblock を Primary/Backup に書込（CLEAN=1, INCOMPLETE=0）
    if (!finalize_superblocks(L, opt))
    {
        m_con.println("mkfs: finalize_superblocks failed");
        return FsStatus::IoError;
    }

    // 6) フラッシュ
    if (!m_dev.flush(m_con))
    {
        m_con.println("mkfs: flush failed (continuing)");
    }

    m_con.println("mkfs: Sylph1FS format complete");
    return FsStatus::Ok;
}