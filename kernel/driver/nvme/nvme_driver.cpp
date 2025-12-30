#include "driver/nvme/nvme_driver.hpp"
#include "arch/inasm.hpp"
#include "memory/memory_manager.hpp"
#include "printk.hpp"

namespace NVMe
{
Driver *g_nvme = nullptr;

Driver::Driver(uintptr_t mmio_base)
    : regs_(reinterpret_cast<volatile Registers *>(mmio_base)),
      admin_sq_(nullptr), admin_cq_(nullptr), sq_tail_(0), cq_head_(0),
      phase_(1), io_sq_(nullptr), io_cq_(nullptr), io_sq_tail_(0),
      io_cq_head_(0), io_phase_(1)
{ // Phaseは1初期化

    // ドアベルレジスタの位置を計算
    // Admin SQ Tail Doorbell = MMIO Start + 0x1000
    // Admin CQ Head Doorbell = MMIO Start + 0x1000 + (1 * Stride)
    // ※QEMUや一般的なPCではStride=4bytesなので +4
    uintptr_t base = reinterpret_cast<uintptr_t>(regs_);
    sq_doorbell_ = reinterpret_cast<volatile uint32_t *>(base + 0x1000);
    cq_doorbell_ = reinterpret_cast<volatile uint32_t *>(base + 0x1004);
    io_sq_doorbell_ = reinterpret_cast<volatile uint32_t *>(base + 0x1008);
    io_cq_doorbell_ = reinterpret_cast<volatile uint32_t *>(base + 0x100C);
}

void Driver::Initialize()
{
    kprintf("[NVMe] Initializing...\n");

    // コントローラを無効化 (リセット)
    DisableController();

    // Admin Queue用のメモリを確保
    // Submission Queue
    admin_sq_ = static_cast<SubmissionQueueEntry *>(MemoryManager::Allocate(
        sizeof(SubmissionQueueEntry) * queue_depth_, 4096));
    // Completion Queue
    admin_cq_ = static_cast<CompletionQueueEntry *>(MemoryManager::Allocate(
        sizeof(CompletionQueueEntry) * queue_depth_, 4096));

    if (!admin_sq_ || !admin_cq_)
    {
        kprintf("[NVMe] Memory Allocation Failed!\n");
        while (1)
            ;
    }

    uint8_t *p = reinterpret_cast<uint8_t *>(admin_sq_);
    for (size_t i = 0; i < sizeof(SubmissionQueueEntry) * queue_depth_; ++i)
        p[i] = 0;

    p = reinterpret_cast<uint8_t *>(admin_cq_);
    for (size_t i = 0; i < sizeof(CompletionQueueEntry) * queue_depth_; ++i)
        p[i] = 0;

    // レジスタにキューのアドレスとサイズを教える
    regs_->asq = reinterpret_cast<uint64_t>(admin_sq_);
    regs_->acq = reinterpret_cast<uint64_t>(admin_cq_);

    // AQA (Admin Queue Attributes)
    // 16-27bit: ACQS (Completion Queue Size - 1)
    // 00-11bit: ASQS (Submission Queue Size - 1)
    uint32_t aqa = 0;
    aqa |= (queue_depth_ - 1) << 16; // ACQS
    aqa |= (queue_depth_ - 1);       // ASQS
    regs_->aqa = aqa;

    // コントローラを有効化
    EnableController();

    kprintf("[NVMe] Initialization Complete. Controller is Ready.\n");
}

void Driver::IdentifyController()
{
    kprintf("[NVMe] Sending Identify Command...\n");

    // データを受け取るためのメモリを確保 (4KB, アライメント4KB)
    auto *identify_data = static_cast<IdentifyControllerData *>(
        MemoryManager::Allocate(sizeof(IdentifyControllerData), 4096));

    // コマンドを作成 (Submission Queueに書き込む)
    SubmissionQueueEntry &cmd = admin_sq_[sq_tail_];

    // メモリクリア
    uint8_t *p = reinterpret_cast<uint8_t *>(&cmd);
    for (size_t i = 0; i < sizeof(cmd); ++i)
        p[i] = 0;

    cmd.opcode = 0x06;  // Identify
    cmd.command_id = 1; // 適当なID

    // Identify Controller (CNS=1)
    // cdw10のBit 0を1にするのではない。CNSフィールドはByte 0。
    // spec: CDW10[07:00] = CNS (Controller or Namespace Structure)
    cmd.cdw10 = 1;

    // データ転送先アドレス (PRP1)
    cmd.data_ptr[0] = reinterpret_cast<uint64_t>(identify_data);

    // ドアベルを鳴らす (SQ Tailを進める)
    sq_tail_++;
    if (sq_tail_ >= queue_depth_)
        sq_tail_ = 0; // リングバッファ
    *sq_doorbell_ = sq_tail_;

    // 完了を待つ (ポーリング)
    kprintf("[NVMe] Polling for completion...");

    while (true)
    {
        // Completion Queueの現在のHeadを見る
        volatile CompletionQueueEntry &cqe = admin_cq_[cq_head_];

        // Phase Tag (Statusフィールドの最上位ビット) を確認
        // Controllerが書き込むと、このビットが現在のphase_と一致する
        uint16_t status = cqe.status;
        uint8_t p_bit =
            (status >> 1) & 1; // Phase Tag (bit 16 in little endian layout?)
        // ※ packed structのstatusはuint16_tなので、bit 0 は "Phase Tag"
        // NVMe Spec: Status Field is 16bits. Bit 0 is Phase Tag (P).

        if ((status & 1) == phase_)
        {
            break;
        }
        PAUSE();
    }
    kprintf(" Done.\n");

    // 完了キューを進める
    cq_head_++;
    if (cq_head_ >= queue_depth_)
    {
        cq_head_ = 0;
        phase_ = !phase_;
    }
    *cq_doorbell_ = cq_head_;

    // 結果表示
    char model[41];
    for (int i = 0; i < 40; ++i)
        model[i] = identify_data->mn[i];
    model[40] = '\0';

    char serial[21];
    for (int i = 0; i < 20; ++i)
        serial[i] = identify_data->sn[i];
    serial[20] = '\0';

    kprintf("[NVMe] Model : %s\n", model);
    kprintf("[NVMe] Serial: %s\n", serial);

    MemoryManager::Free(identify_data, sizeof(IdentifyControllerData));

    auto *ns_data = static_cast<IdentifyNamespaceData *>(
        MemoryManager::Allocate(sizeof(IdentifyNamespaceData), 4096));

    for (size_t i = 0; i < sizeof(cmd); ++i)
        p[i] = 0;

    cmd.opcode = 0x06;        // Identify
    cmd.nsid = namespace_id_; // NSID=1
    cmd.cdw10 = 0;            // CNS=0 (Identify Namespace)
    cmd.data_ptr[0] = reinterpret_cast<uint64_t>(ns_data);

    SendAdminCommand(cmd);

    // LBAサイズ(セクタサイズ)の計算
    // flbasの下位4bitが、現在使用中のLBA Formatのインデックス
    uint8_t lbaf_idx = ns_data->flbas & 0x0F;
    uint8_t ds = ns_data->lbaf[lbaf_idx].ds; // 2の乗数 (9=512, 12=4096)

    lba_size_ = 1 << ds;
    kprintf("[NVMe] LBA Size: %d bytes (Total Blocks: %lld)\n", lba_size_,
            ns_data->nsze);

    MemoryManager::Free(ns_data, sizeof(IdentifyNamespaceData));
}

void Driver::CreateIOQueues()
{
    kprintf("[NVMe] Creating I/O Queues...\n");

    // メモリ確保 (4KBアライメント)
    io_sq_ = static_cast<SubmissionQueueEntry *>(MemoryManager::Allocate(
        sizeof(SubmissionQueueEntry) * queue_depth_, 4096));
    io_cq_ = static_cast<CompletionQueueEntry *>(MemoryManager::Allocate(
        sizeof(CompletionQueueEntry) * queue_depth_, 4096));

    // メモリクリア
    uint8_t *p = reinterpret_cast<uint8_t *>(io_sq_);
    for (size_t i = 0; i < sizeof(SubmissionQueueEntry) * queue_depth_; ++i)
        p[i] = 0;
    p = reinterpret_cast<uint8_t *>(io_cq_);
    for (size_t i = 0; i < sizeof(CompletionQueueEntry) * queue_depth_; ++i)
        p[i] = 0;

    // --- Step A: Create I/O Completion Queue (Opcode 0x05) ---
    SubmissionQueueEntry cmd_cq;
    // 構造体を0クリアする処理を入れるのが安全
    p = reinterpret_cast<uint8_t *>(&cmd_cq);
    for (size_t i = 0; i < sizeof(cmd_cq); ++i)
        p[i] = 0;

    cmd_cq.opcode = 0x05;
    cmd_cq.data_ptr[0] = reinterpret_cast<uint64_t>(io_cq_); // 物理アドレス

    // CDW10: [31:16] Queue Size (0-based), [15:0] Queue ID
    cmd_cq.cdw10 = ((queue_depth_ - 1) << 16) | 1;

    // CDW11: [1] Physically Contiguous (PC) = 1 (連続領域), [0] Interrupt
    // Enabled = 1
    // ※今回はポーリングなので割り込み(IEN)は0でもいいが、作法としてPC=1は必須
    cmd_cq.cdw11 = 1;

    SendAdminCommand(cmd_cq);

    // --- Step B: Create I/O Submission Queue (Opcode 0x01) ---
    SubmissionQueueEntry cmd_sq;
    p = reinterpret_cast<uint8_t *>(&cmd_sq);
    for (size_t i = 0; i < sizeof(cmd_sq); ++i)
        p[i] = 0;

    cmd_sq.opcode = 0x01;
    cmd_sq.data_ptr[0] = reinterpret_cast<uint64_t>(io_sq_);

    // CDW10: [31:16] Queue Size, [15:0] Queue ID
    cmd_sq.cdw10 = ((queue_depth_ - 1) << 16) | 1;

    // CDW11: [16:31] Completion Queue ID (関連付けるCQ), [1] PC=1, [0] QPRI=0
    // CQ ID = 1 とペアにする
    cmd_sq.cdw11 = (1 << 16) | 1;

    SendAdminCommand(cmd_sq);

    kprintf("[NVMe] I/O Queues Created (ID=1).\n");
}

uint64_t *SetupPRPs(SubmissionQueueEntry &cmd, const void *buffer,
                    uint32_t size)
{
    uint64_t addr = reinterpret_cast<uint64_t>(buffer);
    cmd.data_ptr[0] = addr; // PRP1

    const uint32_t page_size = 4096;
    uint32_t offset = addr & (page_size - 1);
    uint32_t page_capacity = page_size - offset;

    if (size <= page_capacity)
    {
        cmd.data_ptr[1] = 0;
        return nullptr;
    }

    // --- 修正ポイント ---

    uint32_t remaining = size - page_capacity;

    // 次のページ（2ページ目）の物理アドレス
    uint64_t next_page_addr = (addr & ~(uint64_t)(page_size - 1)) + page_size;

    // 残りが1ページ以内に収まる場合 (つまり全体で2ページ)
    // PRP2 は「PRP Listへのポインタ」ではなく「データの続き」を直接指す
    if (remaining <= page_size)
    {
        cmd.data_ptr[1] = next_page_addr;
        return nullptr;
    }

    // --- 3ページ以上必要な場合のみ PRP List を作成 ---

    uint32_t num_pages = (remaining + page_size - 1) / page_size;
    uint64_t *prp_list =
        static_cast<uint64_t *>(MemoryManager::Allocate(page_size, page_size));

    cmd.data_ptr[1] = reinterpret_cast<uint64_t>(prp_list);

    uint64_t current_page = next_page_addr;
    for (uint32_t i = 0; i < num_pages; ++i)
    {
        prp_list[i] = current_page;
        current_page += page_size;
    }

    return prp_list;
}

void Driver::ReadLBA(uint64_t lba, void *buffer, uint16_t count)
{
    if (count == 0)
    {
        kprintf("[NVMe] Warning: Read called with count 0. Ignored.\n");
        return;
    }
    // kprintf("[NVMe DEBUG] Read LBA: %lx, Buf: %lx, Cnt: %d\n", lba,
    // (uint64_t)buffer, count);
    SubmissionQueueEntry cmd;
    uint8_t *p = reinterpret_cast<uint8_t *>(&cmd);
    for (size_t i = 0; i < sizeof(cmd); ++i)
        p[i] = 0;

    cmd.opcode = 0x02; // Read
    cmd.nsid = namespace_id_;

    cmd.cdw10 = lba & 0xFFFFFFFF;
    cmd.cdw11 = (lba >> 32) & 0xFFFFFFFF;
    cmd.cdw12 = (count - 1) & 0xFFFF;

    uint32_t size = count * lba_size_;
    uint64_t *prp_list = SetupPRPs(cmd, buffer, size);

    SendIOCommand(cmd);

    if (prp_list != nullptr)
    {
        MemoryManager::Free(prp_list, 4096);
    }
}

void Driver::WriteLBA(uint64_t lba, const void *buffer, uint16_t count)
{
    if (count == 0)
    {
        kprintf("[NVMe] Warning: Write called with count 0. Ignored.\n");
        return;
    }
    // kprintf("[NVMe DEBUG] Write LBA: %lx, Buf: %lx, Cnt: %d\n", lba,
    // (uint64_t)buffer, count);
    SubmissionQueueEntry cmd;
    uint8_t *p = reinterpret_cast<uint8_t *>(&cmd);
    for (size_t i = 0; i < sizeof(cmd); ++i)
        p[i] = 0;

    cmd.opcode = 0x01; // Write
    cmd.nsid = namespace_id_;

    cmd.cdw10 = lba & 0xFFFFFFFF;
    cmd.cdw11 = (lba >> 32) & 0xFFFFFFFF;
    cmd.cdw12 = (count - 1) & 0xFFFF;

    uint32_t size = count * lba_size_;
    uint64_t *prp_list = SetupPRPs(cmd, buffer, size);

    SendIOCommand(cmd);

    if (prp_list != nullptr)
    {
        MemoryManager::Free(prp_list, 4096);
    }
}

void Driver::DisableController()
{
    // CC.EN (bit 0) を 0 にする
    uint32_t cc = regs_->cc;
    if (cc & 1)
    {
        regs_->cc = cc & ~1U;
    }

    // CSTS.RDY (bit 0) が 0 になるまで待つ
    kprintf("[NVMe] Waiting for reset...");
    while (regs_->csts & 1)
    {
        PAUSE();
    }
    kprintf(" Done.\n");
}

void Driver::EnableController()
{
    // CC.EN (bit 0) を 1 にする
    // また、IOCQES(bit 23:20)=4 (16bytes), IOSQES(bit 19:16)=6 (64bytes)
    // を設定するのが一般的
    uint32_t cc = regs_->cc;
    cc |= 1;         // Enable
    cc |= (4 << 20); // CQ Entry Size = 2^4 = 16
    cc |= (6 << 16); // SQ Entry Size = 2^6 = 64
    regs_->cc = cc;

    // CSTS.RDY (bit 0) が 1 になるまで待つ
    kprintf("[NVMe] Waiting for ready...");
    int timeout = 1000000;

    while ((regs_->csts & 1) == 0)
    {
        // エラーチェック: CFS (Controller Fatal Status) ビット (bit 1)
        if (regs_->csts & 2)
        {
            kprintf("\n[NVMe] FATAL ERROR: Controller reported fatal status "
                    "(CSTS=%x)\n",
                    regs_->csts);
            while (1)
                Hlt();
        }

        PAUSE();

        timeout--;
        if (timeout <= 0)
        {
            kprintf("\n[NVMe] TIMEOUT: Controller did not become ready.\n");
            while (1)
                Hlt();
        }
    }
    kprintf(" Done.\n");
}

void Driver::SendAdminCommand(SubmissionQueueEntry &cmd)
{
    WBINVD();
    // SQにコマンド配置
    admin_sq_[sq_tail_] = cmd;

    // ドアベルを鳴らす
    sq_tail_++;
    if (sq_tail_ >= queue_depth_)
        sq_tail_ = 0;
    *sq_doorbell_ = sq_tail_;

    // 完了待ち (ポーリング)
    while (true)
    {
        volatile CompletionQueueEntry &cqe = admin_cq_[cq_head_];
        if ((cqe.status & 1) == phase_)
            break;
        PAUSE();
    }

    // CQ更新
    cq_head_++;
    if (cq_head_ >= queue_depth_)
    {
        cq_head_ = 0;
        phase_ = !phase_;
    }
    *cq_doorbell_ = cq_head_;

    // エラーチェック (Status Fieldが0以外ならエラー)
    volatile CompletionQueueEntry &cqe_done =
        admin_cq_[cq_head_ == 0 ? queue_depth_ - 1 : cq_head_ - 1];
    if ((cqe_done.status >> 1) != 0)
    { // Phase bit以外を見る
        kprintf("[NVMe] Admin Command Failed! Status=%x\n", cqe_done.status);
    }
}

void Driver::SendIOCommand(SubmissionQueueEntry &cmd)
{
    // kprintf("[NVMe CMD] Op:%x NSID:%d PRP1:%lx CDW10:%x CDW12:%x\n",
    // cmd.opcode, cmd.nsid, cmd.data_ptr[0], cmd.cdw10, cmd.cdw12);
    WBINVD();

    // I/O SQに配置
    io_sq_[io_sq_tail_] = cmd;

    // I/O SQ Doorbellを鳴らす
    io_sq_tail_++;
    if (io_sq_tail_ >= queue_depth_)
        io_sq_tail_ = 0;
    *io_sq_doorbell_ = io_sq_tail_;

    // I/O CQで完了待ち
    while (true)
    {
        volatile CompletionQueueEntry &cqe = io_cq_[io_cq_head_];
        // Phase Tagチェック (Bit 0)
        if ((cqe.status & 1) == io_phase_)
            break;
        PAUSE();
    }

    // I/O CQ更新
    io_cq_head_++;
    if (io_cq_head_ >= queue_depth_)
    {
        io_cq_head_ = 0;
        io_phase_ = !io_phase_;
    }
    *io_cq_doorbell_ = io_cq_head_;

    // エラーチェック
    volatile CompletionQueueEntry &cqe_done =
        io_cq_[io_cq_head_ == 0 ? queue_depth_ - 1 : io_cq_head_ - 1];
    if ((cqe_done.status >> 1) != 0)
    {
        kprintf("[NVMe] I/O Command Failed! Status=%x\n", cqe_done.status);
    }
}

} // namespace NVMe