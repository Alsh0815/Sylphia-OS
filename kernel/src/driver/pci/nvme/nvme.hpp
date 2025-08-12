#pragma once
#include <stdint.h>

class Console;

namespace nvme
{
    bool init(void* bar0_va, Console& con);

    // デバッグ用：初期化後に CAP / VS を取得
    uint64_t cap();
    uint32_t vs();

} // namespace nvme
