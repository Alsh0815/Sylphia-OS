#include "block_registry.hpp"
#include "nvme_block_device.hpp"
#include "../../driver/pci/nvme/nvme.hpp" // プロジェクトの相対に合わせて調整してください
#include "../../console.hpp"
#include <new>

namespace block
{

    BlockDevice *open_nvme_as_block(const NvmeInitParams &p, Console &con)
    {
        if (!p.bar0_va)
        {
            con.println("Block: NVMe BAR0 VA is null");
            return nullptr;
        }
        // NVMe 初期化（管理キューセットアップ + Identify）
        /*
        if (!nvme::init(p.bar0_va, con))
        {
            con.println("Block: NVMe init failed");
            return nullptr;
        }
        // IOキュー（qid=1）作成
        if (!nvme::create_io_queues(con, 64))
        {
            con.println("Block: NVMe create_io_queues failed");
            return nullptr;
        }
        */
        // アダプタ生成
        auto *dev = new NvmeBlockDevice(/*nsid*/ p.nsid ? p.nsid : 1);
        if (!dev)
        {
            con.println("Block: allocation failed for NvmeBlockDevice");
            return nullptr;
        }
        return dev;
    }

} // namespace block
