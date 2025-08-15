#pragma once
#include <cstddef>
#include <stdint.h>

class Console;

namespace nvme
{
    enum WriteFlags : uint32_t
    {
        kWriteNone = 0,
        kWriteFua = 1u << 0, // Force Unit Access
    };

    bool init(void *bar0_va, Console &con);

    // デバッグ用：初期化後に CAP / VS を取得
    uint64_t cap();
    uint32_t vs();
    uint32_t debug_read_vs();

    uint32_t lba_bytes();

    bool create_io_queues(Console &con, uint16_t want_qsize = 64);

    bool flush(uint32_t nsid, Console &con);
    bool read_lba(uint32_t nsid, uint64_t slba, uint16_t nlb,
                  void *buf, size_t buf_bytes, Console &con);
    bool write_lba(uint32_t nsid, uint64_t slba, uint16_t nlb,
                   const void *buf, size_t buf_bytes, uint32_t flags, Console &con);

} // namespace nvme
