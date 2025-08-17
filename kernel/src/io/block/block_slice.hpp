#pragma once
#include <cstdint>
#include "../../console.hpp"
#include "../../kernel_runtime.hpp"
#include "block_device.hpp"

// BlockDevice の 4KiB 論理空間をスライス（オフセット/長さを与えてパーティション view にする）
class BlockDeviceSlice final : public BlockDevice
{
public:
    BlockDeviceSlice(BlockDevice &base, uint64_t first_lba4k, uint64_t blocks4k)
        : m_base(base), m_first(first_lba4k), m_len(blocks4k) {}

    uint64_t logical_block_count_4k() const override { return m_len; }
    uint32_t physical_sector_bytes() const override { return m_base.physical_sector_bytes(); }

    bool read_blocks_4k(uint64_t lba4k, uint32_t count, void *buf, size_t buf_bytes, Console &con) override
    {
        if (lba4k >= m_len)
            return false;
        con.println("DEBUG: Entered BlockDeviceSlice::read_blocks_4k");
        simple_wait(100000000);
        uint64_t end = lba4k + count;
        if (end > m_len)
            return false;
        return m_base.read_blocks_4k(m_first + lba4k, count, buf, buf_bytes, con);
    }

    bool write_blocks_4k(uint64_t lba4k, uint32_t count, const void *buf, size_t buf_bytes,
                         bool fua, WriteVerifyMode verify, Console &con) override
    {
        if (lba4k >= m_len)
            return false;
        uint64_t end = lba4k + count;
        if (end > m_len)
            return false;
        return m_base.write_blocks_4k(m_first + lba4k, count, buf, buf_bytes, fua, verify, con);
    }

    bool flush(Console &con) override
    {
        return m_base.flush(con);
    }

private:
    BlockDevice &m_base;
    uint64_t m_first;
    uint64_t m_len;
};
