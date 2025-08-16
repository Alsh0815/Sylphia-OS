#pragma once

#include <cstddef>
#include <cstdint>

namespace sylph1fs
{
#pragma pack(push, 1)
    struct Superblock
    {
        uint32_t magic;
        uint32_t version;
        uint16_t minor_version;
        uint8_t block_size_log2;
        uint8_t csum_kind;
        uint16_t sb_flags;
        uint16_t reserved0;
        uint32_t features_compat;
        uint32_t features_ro_compat;
        uint32_t features_incompat;
        uint32_t reserved1;
        uint8_t uuid[16];
        char label[32];
        uint64_t total_blocks;
        uint64_t total_inodes;
        uint32_t inode_size;
        uint32_t reserved2;
        uint64_t root_inode;
        uint64_t sb_primary_lba4k;
        uint64_t sb_backup_lba4k;
        uint64_t bm_inode_start;
        uint64_t bm_inode_blocks;
        uint64_t bm_data_start;
        uint64_t bm_data_blocks;
        uint64_t inode_table_start;
        uint64_t inode_table_blocks;
        uint64_t data_area_start;
        uint64_t data_area_blocks;
        uint64_t crc_area_start;
        uint64_t crc_area_blocks;
        uint64_t alloc_hint_data;
        uint64_t alloc_hint_inode;
        uint8_t dirhash_secret[16];
        uint8_t reserved3[3852];
        uint32_t sb_crc32c;
    };
#pragma pack(pop)

#pragma pack(push, 1)
    struct Extent
    {
        uint64_t start_block_rel;
        uint32_t length_blocks;
        uint32_t reserved;
    };
#pragma pack(pop)

#pragma pack(push, 1)
    struct Inode
    {
        uint64_t inode_id;
        uint16_t mode;
        uint16_t links;
        uint32_t uid;
        uint32_t gid;
        uint32_t flags;
        uint64_t size_bytes;
        uint64_t atime;
        uint64_t mtime;
        uint64_t ctime;
        uint16_t extent_count;
        uint16_t reserved0;
        uint32_t reserved1;
        Extent extents_inline[4];
        uint64_t overflow_extents_block;
        uint64_t xattr_block;
        uint32_t dir_format;
        uint32_t reserved2;
        uint64_t dir_header_block;
        uint8_t reserved3[92];
        uint32_t inode_crc32c;
    };

#pragma pack(pop)

    static_assert(offsetof(Superblock, magic) == 0x000, "Superblock magic offset mismatch");
    static_assert(offsetof(Superblock, version) == 0x004, "Superblock version offset mismatch");
    static_assert(offsetof(Superblock, minor_version) == 0x008, "Superblock minor_version offset mismatch");
    static_assert(offsetof(Superblock, block_size_log2) == 0x00A, "Superblock block_size_log2 offset mismatch");
    static_assert(offsetof(Superblock, csum_kind) == 0x00B, "Superblock csum_kind offset mismatch");
    static_assert(offsetof(Superblock, sb_flags) == 0x00C, "Superblock sb_flags offset mismatch");
    static_assert(offsetof(Superblock, features_compat) == 0x010, "Superblock features_compat offset mismatch");
    static_assert(offsetof(Superblock, features_ro_compat) == 0x014, "Superblock features_ro_compat offset mismatch");
    static_assert(offsetof(Superblock, features_incompat) == 0x018, "Superblock features_incompat offset mismatch");
    static_assert(offsetof(Superblock, uuid) == 0x020, "Superblock uuid offset mismatch");
    static_assert(offsetof(Superblock, label) == 0x030, "Superblock label offset mismatch");
    static_assert(offsetof(Superblock, total_blocks) == 0x050, "Superblock total_blocks offset mismatch");
    static_assert(offsetof(Superblock, total_inodes) == 0x058, "Superblock total_inodes offset mismatch");
    static_assert(offsetof(Superblock, root_inode) == 0x068, "Superblock root_inode offset mismatch");
    static_assert(offsetof(Superblock, sb_primary_lba4k) == 0x070, "Superblock sb_primary_lba4k offset mismatch");
    static_assert(offsetof(Superblock, sb_backup_lba4k) == 0x078, "Superblock sb_backup_lba4k offset mismatch");
    static_assert(offsetof(Superblock, bm_inode_start) == 0x080, "Superblock bm_inode_start offset mismatch");
    static_assert(offsetof(Superblock, bm_inode_blocks) == 0x088, "Superblock bm_inode_blocks offset mismatch");
    static_assert(offsetof(Superblock, bm_data_start) == 0x090, "Superblock bm_data_start offset mismatch");
    static_assert(offsetof(Superblock, bm_data_blocks) == 0x098, "Superblock bm_data_blocks offset mismatch");
    static_assert(offsetof(Superblock, inode_table_start) == 0x0A0, "Superblock inode_table_start offset mismatch");
    static_assert(offsetof(Superblock, inode_table_blocks) == 0x0A8, "Superblock inode_table_blocks offset mismatch");
    static_assert(offsetof(Superblock, data_area_start) == 0x0B0, "Superblock data_area_start offset mismatch");
    static_assert(offsetof(Superblock, data_area_blocks) == 0x0B8, "Superblock data_area_blocks offset mismatch");
    static_assert(offsetof(Superblock, crc_area_start) == 0x0C0, "Superblock crc_area_start offset mismatch");
    static_assert(offsetof(Superblock, crc_area_blocks) == 0x0C8, "Superblock crc_area_blocks offset mismatch");
    static_assert(offsetof(Superblock, alloc_hint_data) == 0x0D0, "Superblock alloc_hint_data offset mismatch");
    static_assert(offsetof(Superblock, alloc_hint_inode) == 0x0D8, "Superblock alloc_hint_inode offset mismatch");
    static_assert(offsetof(Superblock, dirhash_secret) == 0x0E0, "Superblock dirhash_secret offset mismatch");
    static_assert(offsetof(Superblock, sb_crc32c) == 0xFFC, "Superblock sb_crc32c offset mismatch");
    static_assert(sizeof(Superblock) == 4096, "Superblock struct size mismatch");

    static_assert(offsetof(Extent, start_block_rel) == 0x00, "Extent start_block_rel offset mismatch");
    static_assert(offsetof(Extent, length_blocks) == 0x08, "Extent length_blocks offset mismatch");
    static_assert(sizeof(Extent) == 16, "Extent struct size mismatch");

    static_assert(offsetof(Inode, inode_id) == 0x00, "Inode inode_id offset mismatch");
    static_assert(offsetof(Inode, mode) == 0x08, "Inode mode offset mismatch");
    static_assert(offsetof(Inode, links) == 0x0A, "Inode links offset mismatch");
    static_assert(offsetof(Inode, uid) == 0x0C, "Inode uid offset mismatch");
    static_assert(offsetof(Inode, gid) == 0x10, "Inode gid offset mismatch");
    static_assert(offsetof(Inode, flags) == 0x14, "Inode flags offset mismatch");
    static_assert(offsetof(Inode, size_bytes) == 0x18, "Inode size_bytes offset mismatch");
    static_assert(offsetof(Inode, atime) == 0x20, "Inode atime offset mismatch");
    static_assert(offsetof(Inode, mtime) == 0x28, "Inode mtime offset mismatch");
    static_assert(offsetof(Inode, ctime) == 0x30, "Inode ctime offset mismatch");
    static_assert(offsetof(Inode, extent_count) == 0x38, "Inode extent_count offset mismatch");
    static_assert(offsetof(Inode, extents_inline) == 0x40, "Inode extents_inline offset mismatch");
    static_assert(offsetof(Inode, overflow_extents_block) == 0x80, "Inode overflow_extents_block offset mismatch");
    static_assert(offsetof(Inode, xattr_block) == 0x88, "Inode xattr_block offset mismatch");
    static_assert(offsetof(Inode, dir_format) == 0x90, "Inode dir_format offset mismatch");
    static_assert(offsetof(Inode, dir_header_block) == 0x98, "Inode dir_header_block offset mismatch");
    static_assert(offsetof(Inode, inode_crc32c) == 0xFC, "Inode inode_crc32c offset mismatch");
    static_assert(sizeof(Inode) == 256, "Inode struct size mismatch");
}