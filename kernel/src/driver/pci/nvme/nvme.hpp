#pragma once
#include <stdint.h>

class Console;

namespace nvme
{

    // 初期化：BAR0(物理) に対して Admin SQ/CQ を作って EN=1 にする。
    // 事前に BusMaster/MemSpace 有効化＆必要なら map_mmio_range 済みで呼んでください。
    bool init(uint64_t bar0_phys, Console &con);

    // デバッグ用：初期化後に CAP / VS を取得
    uint64_t cap();
    uint32_t vs();

} // namespace nvme
