#pragma once
#include <stdint.h>

class Console;

namespace nvme
{
    bool init(void *bar0_va, Console &con);

    // デバッグ用：初期化後に CAP / VS を取得
    uint64_t cap();
    uint32_t vs();

    bool create_io_queues(Console &con, uint16_t want_qsize = 64);

} // namespace nvme
