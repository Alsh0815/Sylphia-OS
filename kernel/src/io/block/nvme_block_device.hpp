#pragma once
#include "block_device.hpp"
#include <cstdint>

class Console;

class NvmeBlockDevice final : public BlockDevice
{
public:
    explicit NvmeBlockDevice(uint32_t nsid);

    uint64_t logical_block_count_4k() const override; // いまは未知なら 0 を返す
    uint32_t physical_sector_bytes() const override;

    bool read_blocks_4k(uint64_t lba4k, uint32_t count, void *buf, size_t buf_bytes, Console &con) override;
    bool write_blocks_4k(uint64_t lba4k, uint32_t count, const void *buf, size_t buf_bytes,
                         bool fua, WriteVerifyMode verify, Console &con) override;

    bool flush(Console &con) override;

private:
    uint32_t m_nsid;
    uint32_t m_sector_bytes; // nvme::lba_bytes() のスナップショット（マウント時に確定）
    uint32_t m_4k_to_nlb;    // 4KiB あたりの NVMe LBA 数（例: 512Bなら 8, 4KiBなら 1）

    // 書き込み検証用の一時バッファ（必要時のみ確保/解放）
    bool verify_write_range(uint64_t lba4k, uint32_t count, const void *src, size_t bytes, Console &con);

    // nvme 側の DMA32 制約に当たる時のフォールバック（4KiB×count 分）
    // 成功時: *out_va を DMA32 メモリへコピー済みにする（write）、read 時は呼び出し側で読み戻し後に原バッファへコピー。
    // 戻り値: 成功なら確保したVA、失敗なら nullptr
    void *alloc_dma32_bounce(size_t bytes, Console &con);

    // 物理LBAとnlbを計算（4KiB論理→物理LBA）
    inline void calc_nvme_range(uint64_t lba4k, uint32_t count, uint64_t &out_slba, uint16_t &out_nlb) const
    {
        const uint64_t nlb_total = (uint64_t)count * (uint64_t)m_4k_to_nlb;
        out_slba = (uint64_t)lba4k * (uint64_t)m_4k_to_nlb;
        out_nlb = (uint16_t)(nlb_total & 0xFFFFu); // 1回の呼び出しでは 16bit に収まる数で扱う
    }
};
