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

    const sylph1fs::Superblock &superblock() const { return m_sb; }
    bool read_only() const { return m_ro; }

private:
    BlockDevice &m_dev;
    sylph1fs::Superblock m_sb;
    bool m_ro;

    bool map_crc_entry(uint64_t data_idx, uint64_t &crc_lba4k, size_t &crc_off, Console &con) const;
};