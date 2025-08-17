#pragma once

#include <cstddef>
#include <cstdint>
#include "../../block/block_device.hpp"
#include "../fs_types.hpp"
#include "sylph1fs_structs.hpp"

class Sylph1FS
{
public:
    struct MkfsOptions
    {
        const char *label = nullptr; // UTF-8, 32Bまで。nullptrなら空
        uint32_t version = 1;
        uint16_t minor_version = 0;
        uint64_t total_inodes = 0;            // 0ならヒューリスティク(1 inode / 64KiB)
        uint32_t features_compat = (1u << 0); // bit0=HAS_CHECKSUMS 既定ON
        uint32_t features_ro_compat = 0;
        uint32_t features_incompat = 0;
        uint32_t dir_bucket_count = 256; // ルート用(将来使用)
        const uint8_t *uuid16 = nullptr; // nullptrなら全0（ランダム生成は今は行わない）
    };

    Sylph1FS(BlockDevice &dev, Console &con);

    FsStatus mkfs(const MkfsOptions &opt);

private:
    BlockDevice &m_dev;
    Console &m_con;

    static uint32_t crc32c(const void *data, size_t len);
    bool write_zeros(uint64_t start_lba4k, uint64_t blocks);
    bool write_and_verify(uint64_t lba4k, const void *buf4096);

    // レイアウト計算（2段収束）
    struct Layout
    {
        uint64_t total_blocks;
        uint64_t bm_inode_start, bm_inode_blocks;
        uint64_t bm_data_start, bm_data_blocks;
        uint64_t inode_table_start, inode_table_blocks;
        uint64_t data_area_start, data_area_blocks;
        uint64_t crc_area_start, crc_area_blocks;
        uint64_t sb_primary_lba4k, sb_backup_lba4k;
        uint64_t total_inodes;
    };

    bool compute_layout(Layout &L, uint64_t total_inodes_hint);
    bool write_superblock_initial(const Layout &L, const MkfsOptions &opt);
    bool finalize_superblocks(const Layout &L, const MkfsOptions &opt);
    bool init_root_inode(const Layout &L, const MkfsOptions &opt);
    bool clear_meta_areas(const Layout &L);

    // ルート用の 4KiB ブロックを確保・初期化（DirHeader + BucketTable）し、
    // Data bitmap と CRC Area を更新する。返り値: 成功時 true、data_idx_out に
    // Data area 相対 index を返す（通常 0 を使う）。
    bool allocate_and_init_root_dir_block(const Layout &L,
                                          uint32_t bucket_count,
                                          uint64_t &data_idx_out);

    // CRC Area: data_idx -> (crc_lba4k, crc_off) を算出
    static inline bool crc_map_entry(const Layout &L, uint64_t data_idx,
                                     uint64_t &crc_lba4k, size_t &crc_off)
    {
        if (data_idx >= L.data_area_blocks)
            return false;
        const uint64_t crc_byte_off = data_idx * 4ull;
        crc_lba4k = L.crc_area_start + (crc_byte_off >> 12); // /4096
        crc_off = (size_t)(crc_byte_off & 0xFFFu);           // %4096
        return (crc_lba4k >= L.crc_area_start) &&
               (crc_lba4k < L.crc_area_start + L.crc_area_blocks) &&
               (crc_off <= 4092);
    }
};