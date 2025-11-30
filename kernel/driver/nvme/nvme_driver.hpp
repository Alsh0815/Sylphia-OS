#pragma once
#include <stdint.h>
#include "driver/nvme/nvme_identify.hpp"
#include "driver/nvme/nvme_reg.hpp"
#include "driver/nvme/nvme_queue.hpp"
#include "block_device.hpp"

namespace NVMe
{

    class Driver : public BlockDevice
    {
    public:
        // コンストラクタ: BAR0のアドレスを受け取る
        Driver(uintptr_t mmio_base);

        void Initialize();
        void IdentifyController();

        void CreateIOQueues();

        void ReadLBA(uint64_t lba, void *buffer, uint16_t count);
        void WriteLBA(uint64_t lba, const void *buffer, uint16_t count);

        bool Read(uint64_t lba, void *buffer, uint32_t count) override
        {
            ReadLBA(lba, buffer, count);
            return true;
        }

        bool Write(uint64_t lba, const void *buffer, uint32_t count) override
        {
            WriteLBA(lba, buffer, count);
            return true;
        }

        uint32_t GetBlockSize() const override { return 0; }

    private:
        volatile Registers *regs_; // MMIOレジスタへのアクセサ

        SubmissionQueueEntry *admin_sq_;
        CompletionQueueEntry *admin_cq_;
        uint16_t sq_tail_; // 次に命令を書く場所
        uint16_t cq_head_; // 次に完了を確認する場所
        uint8_t phase_;    // 完了キューのPhase Tag (1から始まる)
        volatile uint32_t *sq_doorbell_;
        volatile uint32_t *cq_doorbell_;

        SubmissionQueueEntry *io_sq_;
        CompletionQueueEntry *io_cq_;
        uint16_t io_sq_tail_;
        uint16_t io_cq_head_;
        uint8_t io_phase_;
        volatile uint32_t *io_sq_doorbell_;
        volatile uint32_t *io_cq_doorbell_;

        const uint16_t queue_depth_ = 32; // キューのサイズ

        uint32_t namespace_id_ = 1; // 通常は1
        uint32_t lba_size_ = 512;   // デフォルト512B (Identifyで更新)

        void DisableController();
        void EnableController();

        void SendAdminCommand(SubmissionQueueEntry &cmd);
        void SendIOCommand(SubmissionQueueEntry &cmd);
    };

    extern Driver *g_nvme;

}