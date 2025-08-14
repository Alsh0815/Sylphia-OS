#include "nvme_block_device.hpp"
#include "../../driver/pci/nvme/nvme.hpp"
#include "../../console.hpp"
#include "../../pmm.hpp"
#include "../../paging.hpp"
#include <cstring>

NvmeBlockDevice::NvmeBlockDevice(uint32_t nsid)
    : m_nsid(nsid), m_sector_bytes(nvme::lba_bytes())
{
    // 4KiB がデバイスセクタの整数倍であることを確認（そうでなければ使用不可）
    if (m_sector_bytes == 0 || (4096u % m_sector_bytes) != 0)
    {
        // ここでは 0 をセットして read/write は失敗させる
        m_4k_to_nlb = 0;
    }
    else
    {
        m_4k_to_nlb = 4096u / m_sector_bytes;
    }
}

uint64_t NvmeBlockDevice::logical_block_count_4k() const
{
    // ここでは容量取得手段が公開されていないため 0 を返す（将来拡張で Identify NS の nsze を使って計算可能）
    return 0;
}

uint32_t NvmeBlockDevice::physical_sector_bytes() const
{
    return m_sector_bytes;
}

void *NvmeBlockDevice::alloc_dma32_bounce(size_t bytes, Console &con)
{
    // 必要ページ数を概算
    size_t pages = (bytes + 4095u) / 4096u;
    void *va = pmm::alloc_pages((uint32_t)pages);
    if (!va)
    {
        con.println("Block(NVMe): DMA32 bounce alloc failed");
        return nullptr;
    }
    // すべてのページが <4GiB の物理アドレスで 4KiB アラインかを確認
    uint8_t *p = reinterpret_cast<uint8_t *>(va);
    for (size_t i = 0; i < pages; ++i)
    {
        uint64_t pa = paging::virt_to_phys((uint64_t)(uintptr_t)(p + i * 4096u));
        if ((pa & 0xFFFu) != 0 || (pa >> 32) != 0)
        {
            // 低位物理が取れなかった場合は失敗（戻すAPIがあるならここで解放）
            // pmm::free_pages(va, (uint32_t)pages);
            con.println("Block(NVMe): bounce page not DMA32-aligned/located");
            return nullptr;
        }
    }
    return va;
}

bool NvmeBlockDevice::read_blocks_4k(uint64_t lba4k, uint32_t count, void *buf, size_t buf_bytes, Console &con)
{
    if (m_4k_to_nlb == 0)
    {
        con.println("Block(NVMe): sector size incompatible");
        return false;
    }
    const size_t need = (size_t)count * 4096u;
    if (buf_bytes < need)
    {
        con.printf("Block(NVMe): read buffer too small (need %u)\n", (unsigned)need);
        return false;
    }

    uint64_t slba = 0;
    uint16_t nlb = 0;
    calc_nvme_range(lba4k, count, slba, nlb);

    // NVMe 実装は PRP の各ページが DMA32 であることを要求している。
    // もし上位物理に落ちる可能性がある場合はバウンスで受ける。
    // ただし「読み取り」は直接 buf へ入れられればコピー不要なので、まずは直読みを試す。
    // 直読みが DMA32 不適合で失敗した場合に限ってバウンスへフォールバックする方針。
    if (nvme::read_lba(m_nsid, slba, nlb, buf, need, con))
    {
        return true;
    }

    // フォールバック：DMA32 バウンスを介して読む
    void *bounce = alloc_dma32_bounce(need, con);
    if (!bounce)
        return false;
    if (!nvme::read_lba(m_nsid, slba, nlb, bounce, need, con))
    {
        // pmm::free_pages(bounce, pages) // APIがあれば解放
        return false;
    }
    memcpy(buf, bounce, need);
    // pmm::free_pages(bounce, pages)
    return true;
}

bool NvmeBlockDevice::verify_write_range(uint64_t lba4k, uint32_t count, const void *src, size_t bytes, Console &con)
{
    // 一時バッファを使って読み戻し → 比較
    void *tmp = alloc_dma32_bounce(bytes, con);
    if (!tmp)
        return false;
    if (!read_blocks_4k(lba4k, count, tmp, bytes, con))
        return false;
    int cmp = memcmp(tmp, src, bytes);
    if (cmp != 0)
    {
        con.println("Block(NVMe): write verify mismatch");
        return false;
    }
    return true;
}

bool NvmeBlockDevice::write_blocks_4k(uint64_t lba4k, uint32_t count, const void *buf, size_t buf_bytes,
                                      bool fua, WriteVerifyMode verify, Console &con)
{
    if (m_4k_to_nlb == 0)
    {
        con.println("Block(NVMe): sector size incompatible");
        return false;
    }
    const size_t need = (size_t)count * 4096u;
    if (buf_bytes < need)
    {
        con.printf("Block(NVMe): write buffer too small (need %u)\n", (unsigned)need);
        return false;
    }

    uint64_t slba = 0;
    uint16_t nlb = 0;
    calc_nvme_range(lba4k, count, slba, nlb);

    // まずは直接書き込みを試す
    uint32_t flags = fua ? nvme::kWriteFua : nvme::kWriteNone;
    if (!nvme::write_lba(m_nsid, slba, nlb, buf, need, flags, con))
    {
        // フォールバック：DMA32 バウンスにコピーしてから書く
        void *bounce = alloc_dma32_bounce(need, con);
        if (!bounce)
            return false;
        std::memcpy(bounce, buf, need);
        if (!nvme::write_lba(m_nsid, slba, nlb, bounce, need, flags, con))
        {
            return false;
        }
    }

    if (verify == kVerifyAfterWrite)
    {
        if (!verify_write_range(lba4k, count, buf, need, con))
            return false;
    }
    return true;
}

bool NvmeBlockDevice::flush(Console &con)
{
    return nvme::flush(m_nsid, con);
}
