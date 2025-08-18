#pragma once

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
    bool readdir_root(Console &con);
    bool readdir_dir(uint64_t dir_inode_id, Console &con);
    bool readdir_path(const char *abs_path, Console &con);
    bool lookup_in_root(const char *name, uint64_t &inode_out, uint16_t &type_out, Console &con);

    bool lookup_in_dir(uint64_t dir_inode_id, const char *name, uint64_t &inode_out, uint16_t &type_out, Console &con);
    bool dir_add_entry(uint64_t parent_inode_id, const char *name, uint16_t type, uint64_t child_ino, Console &con);
    bool mkdir_path(const char *abs_path, Console &con);
    bool create_path(const char *abs_path, Console &con);

    bool unlink_path(const char *abs_path, Console &con);
    bool rmdir_path(const char *abs_path, Console &con);

    const sylph1fs::Superblock &superblock() const { return m_sb; }
    bool read_only() const { return m_ro; }

    bool test_create(const char *name, Console &con);
    bool test_mkdir(const char *name, Console &con);

private:
    BlockDevice &m_dev;
    sylph1fs::Superblock m_sb;
    bool m_ro;

    bool map_crc_entry(uint64_t data_idx, uint64_t &crc_lba4k, size_t &crc_off, Console &con) const;

    bool write_block_with_sidecar_crc(uint64_t data_idx, const void *buf4096, Console &con);

    bool enumerate_slab(uint64_t slab_idx, Console &con, uint32_t &out_count);
    bool append_entry_with_spill(uint64_t slab_idx, const char *name, uint16_t type, uint64_t child_ino, Console &con);
    bool alloc_data_blocks(uint32_t need, uint64_t &start_idx, Console &con); // first-fit、確保は最後に commit
    bool set_data_bitmap_range(uint64_t start_idx, uint32_t count, bool used, Console &con);
    bool alloc_inode(uint64_t &out_id, Console &con);
    bool write_inode(const sylph1fs::Inode &ino, Console &con);
    bool set_inode_bitmap(uint64_t inode_id, bool used, Console &con);

    bool init_dir_block(uint32_t bucket_count, uint64_t &data_idx_out, Console &con);
    bool dir_add_entry_root(const char *name, uint16_t type, uint64_t child_ino, Console &con);

    bool split_parent_basename(const char *abs_path, uint64_t &parent_ino, char *base_out, size_t &base_len, Console &con);
    bool resolve_path_inode(const char *abs_path, uint64_t &inode_out, uint16_t &type_out, Console &con);

    bool dir_remove_entry(uint64_t parent_inode_id, const char *name, uint16_t &type_out, uint64_t &child_ino_out, Console &con);
    bool is_dir_empty(uint64_t dir_inode_id, Console &con);
};