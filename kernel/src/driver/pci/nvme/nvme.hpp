#pragma once
#include <cstddef>
#include <stdint.h>

class Console;

namespace nvme
{
    bool init(void *bar0_va, Console &con);

    // デバッグ用：初期化後に CAP / VS を取得
    uint64_t cap();
    uint32_t vs();

    bool create_io_queues(Console &con, uint16_t want_qsize = 64);

    bool read_lba(uint32_t nsid, uint64_t slba, uint16_t nlb,
                  void *buf, size_t buf_bytes, Console &con);

} // namespace nvme
