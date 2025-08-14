#pragma once
#include <cstdint>

class Console;
class BlockDevice;

namespace block
{

    // いずれ USB/SATA/SCSI などもここに登録する。
    // いまは NVMe のみ対応。bar0_va を与えて初期化する形。
    struct NvmeInitParams
    {
        void *bar0_va; // NVMe BAR0 のマップ済みVA
        uint32_t nsid; // 既定は 1 を推奨
    };

    // 何らかの「既知のデバイス」を開く（現状 NVMe 専用の明示IF）
    // 成功時に BlockDevice* を返す（delete で破棄可能）。
    BlockDevice *open_nvme_as_block(const NvmeInitParams &p, Console &con);

} // namespace block
