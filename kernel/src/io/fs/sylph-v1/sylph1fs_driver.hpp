#pragma once

#include "../../../pmm_vector.hpp"
#include "../vfs.hpp"           // FsDriver / FsMount / vfs::register_driver
#include "sylph1fs.hpp"         // Sylph1FS（将来、mount後に流用予定）
#include "sylph1fs_structs.hpp" // on-disk Superblock 定義

class Sylph1FsDriver : public FsDriver
{
public:
    const char *name() const override { return "Sylph1FS"; }
    bool probe(BlockDevice &device, Console &con) override;
    FsMount *mount(BlockDevice &device, Console &con) override;
};

bool register_sylph1fs_driver();

struct SylphStat
{
    uint16_t type;     // sylph1fs::kDirEntTypeDir(1) or kDirEntTypeFile(2)
    uint16_t mode;     // 上位での権限/種別（inode.modeそのまま）
    uint32_t links;    // ハードリンク数
    uint64_t size;     // バイト数
    uint64_t inode_id; // inode番号
    uint64_t ctime;    // 未実装→0
    uint64_t mtime;    // 未実装→0
    uint64_t atime;    // 未実装→0
};

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

    bool read_data_block(uint64_t data_idx, void *buf4096, Console &con) const;
    bool verify_data_block_crc(uint64_t data_idx, const void *buf4096, Console &con) const;
    bool read_inode(uint64_t inode_id, sylph1fs::Inode &out, Console &con) const;
    bool readdir_dir(uint64_t dir_inode_id, Console &con);
    bool readdir_path(const char *abs_path, Console &con);

    bool lookup_in_dir(uint64_t dir_inode_id, const char *name, uint64_t &inode_out, uint16_t &type_out, Console &con);
    bool dir_add_entry(uint64_t parent_inode_id, const char *name, uint16_t type, uint64_t child_ino, Console &con);
    bool mkdir_path(const char *abs_path, Console &con) override;
    bool create_path(const char *abs_path, Console &con) override;

    bool write_path(const char *abs_path, const void *buf, uint64_t len, uint64_t off, Console &con) override;
    bool read_path(const char *abs_path, void *buf, uint64_t len, uint64_t off, Console &con) override;
    bool truncate_path(const char *abs_path, uint64_t new_size, Console &con) override;

    bool unlink_path(const char *abs_path, Console &con) override;
    bool rmdir_path(const char *abs_path, Console &con) override;

    bool rename_path(const char *old_path, const char *new_path, Console &con) override;
    bool stat_path(const char *abs_path, VfsStat &st, Console &con) override;

    const sylph1fs::Superblock &superblock() const { return m_sb; }
    bool read_only() const { return m_ro; }

private:
    BlockDevice &m_dev;
    sylph1fs::Superblock m_sb;
    bool m_ro;

    bool check_dir_crc(uint64_t inode_id, const char *name, size_t &nlen, sylph1fs::Inode &parent, sylph1fs::DirHeader *&hdr, uint8_t *hdrblk, uint64_t &hdr_idx, Console &con);
    bool map_crc_entry(uint64_t data_idx, uint64_t &crc_lba4k, size_t &crc_off, Console &con) const;
    bool clear_sidecar_crcs(uint64_t start_data_idx, uint32_t count, Console &con);

    bool write_block_with_sidecar_crc(uint64_t data_idx, const void *buf4096, Console &con);

    bool update_dotdot_entry(uint64_t dir_inode_id, uint64_t new_parent_inode_id, Console &con);

    bool enumerate_slab(uint64_t slab_idx, Console &con, uint32_t &out_count);
    bool append_entry_with_spill(uint64_t slab_idx, const char *name, uint16_t type, uint64_t child_ino, bool *out_spilled, Console &con);
    bool alloc_data_blocks(uint32_t need, uint64_t &start_idx, Console &con); // first-fit、確保は最後に commit
    bool set_data_bitmap_range(uint64_t start_idx, uint32_t count, bool used, Console &con);
    bool alloc_inode(uint64_t &out_id, Console &con);
    bool write_inode(const sylph1fs::Inode &ino, Console &con);
    bool set_inode_bitmap(uint64_t inode_id, bool used, Console &con);

    bool init_dir_block(uint32_t bucket_count, uint64_t &data_idx_out, Console &con);

    bool split_parent_basename(const char *abs_path, uint64_t &parent_ino, char *base_out, size_t &base_len, Console &con);
    bool resolve_path_inode(const char *abs_path, uint64_t &inode_out, uint16_t &type_out, Console &con);

    bool dir_remove_entry(uint64_t parent_inode_id, const char *name, uint16_t &type_out, uint64_t &child_ino_out, Console &con);
    bool is_dir_empty(uint64_t dir_inode_id, Console &con);
    bool free_file_storage(sylph1fs::Inode &ino, uint64_t start_offset_to_free, Console &con);
    bool free_dir_storage(uint64_t dir_inode_id, Console &con);

    bool load_all_extents(const sylph1fs::Inode &ino, PmmVec<sylph1fs::Extent> &out, Console &con);

    bool allocate_file_blocks_and_attach(sylph1fs::Inode &ino, uint64_t need_blocks, Console &con);
    bool append_extent_to_overflow(uint64_t ofb_idx, const sylph1fs::Extent &e, uint64_t &tail_idx_out, Console &con);
    bool ensure_overflow_block(sylph1fs::Inode &ino, uint64_t &ofb_idx_out, Console &con);
    bool pwrite_file(const uint64_t inode_id, const void *src, uint64_t off, uint64_t len, Console &con);
    bool pread_file_block(const sylph1fs::Inode &ino, uint64_t file_block_idx, void *out4096, Console &con);
};