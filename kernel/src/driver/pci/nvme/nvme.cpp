#include "nvme.hpp"
#include "nvme_regs.hpp"
#include "../../../paging.hpp"
#include "../../../pmm.hpp"
#include "../../../console.hpp"

extern "C" void *memcpy(void *d, const void *s, size_t n);

namespace nvme
{

    struct SqEntry
    {
        uint8_t opc;
        uint8_t fuse;
        uint16_t cid;
        uint32_t nsid;
        uint64_t rsv2;
        uint64_t mptr;
        uint64_t prp1;
        uint64_t prp2;
        uint32_t cdw10, cdw11, cdw12, cdw13, cdw14, cdw15;
    } __attribute__((packed));

    struct CqEntry
    {
        uint32_t dw0, dw1;
        uint16_t sq_head, sq_id;
        uint16_t cid;
        uint16_t status; // bit0: phase
    } __attribute__((packed));

    struct Ctx
    {
        volatile NvmeRegs *r;
        uint32_t db_stride; // 4 << DSTRD
        SqEntry *asq;
        CqEntry *acq;
        uint16_t qsize;   // 実エントリ数（例: 32）
        uint16_t sq_tail; // 0..qsize-1
        uint16_t cq_head; // 0..qsize-1
        uint8_t cq_phase; // 初期1
        uint64_t cap_cache;
        uint32_t vs_cache;
        // --- I/O Queues (qid=1) ---
        SqEntry *io_sq = nullptr;
        CqEntry *io_cq = nullptr;
        uint16_t io_qsize = 0;
        uint16_t io_sq_tail = 0;
        uint16_t io_cq_head = 0;
        uint8_t io_cq_phase = 1;
        uint64_t io_sq_phys = 0;
        uint64_t io_cq_phys = 0;
        // Set Features Number of Queues (result)
        uint16_t nsqr = 0;                     // 実際に使える Submission Queues の数
        uint16_t ncqr = 0;                     // 実際に使える Completion Queues の数
        uint32_t ns_active = 1;                // 使っているNS
        uint32_t lba_bytes = 512;              // Identifyで更新、未取得時のフォールバック=512B
        uint8_t mdts = 0;                      // Identify Controller: MDTS (exponent)
        uint32_t max_xfer_bytes = 0xFFFFFFFFu; // 計算済みの最大転送長（バイト）
    } g{};

    // ★ サイズ検証（仕様: SQ=64B, CQ=16B）
    static_assert(sizeof(SqEntry) == 64, "SQ entry size must be 64 bytes");
    static_assert(sizeof(CqEntry) == 16, "CQ entry size must be 16 bytes");

    static void dump_nvme_status(Console &con, uint16_t st_shifted)
    {
        // st_shifted は status >> 1 渡し
        uint8_t sc = st_shifted & 0xFF;        // SC[7:0]
        uint8_t sct = (st_shifted >> 8) & 0x7; // SCT[2:0]
        con.printf("  -> status: SCT=%u SC=%x\n", (unsigned)sct, (unsigned)sc);
    }

    static void dump_sqe(Console &con, const SqEntry &e)
    {
        const uint32_t *d = reinterpret_cast<const uint32_t *>(&e);
        con.printf("SQE:");
        for (int i = 0; i < 16; i++)
        {
            if ((i % 4) == 0)
                con.printf("\n  DW%02d:", i);
            con.printf(" %08x", (unsigned)d[i]);
        }
        con.printf("\n");
    }

    static void tiny_pause()
    {
        for (volatile int i = 0; i < 2000000; i++)
            asm volatile("");
    }

    static inline void bzero(void *p, size_t n)
    {
        uint8_t *q = (uint8_t *)p;
        for (size_t i = 0; i < n; i++)
            q[i] = 0;
    }
    static inline void mmio_wmb() { asm volatile("" ::: "memory"); } // 必要十分の可視化

    // ドアベル計算: BAR0 + 0x1000 + stride*(2*qid + 0/1)
    static volatile uint32_t *doorbell_sq(uint16_t qid)
    {
        uintptr_t base = (uintptr_t)g.r;
        return (volatile uint32_t *)(base + 0x1000 + g.db_stride * (2 * qid + 0));
    }
    static volatile uint32_t *doorbell_cq(uint16_t qid)
    {
        uintptr_t base = (uintptr_t)g.r;
        return (volatile uint32_t *)(base + 0x1000 + g.db_stride * (2 * qid + 1));
    }

    static bool wait_rdy(int want, uint32_t loops)
    {
        for (uint32_t i = 0; i < loops; ++i)
        {
            if ((g.r->CSTS & 1u) == (uint32_t)want)
                return true;
        }
        return false;
    }

    static bool io_wait_complete(Console &con, uint16_t &out_status, CqEntry &out_ce)
    {
        for (uint32_t spin = 0; spin < 10000000; ++spin)
        {
            const volatile CqEntry *ce = &g.io_cq[g.io_cq_head];

            // volatileなstatusを読み込む
            uint16_t st = ce->status;

            if ((st & 1) == g.io_cq_phase)
            {
                // memcpyを使わず、フィールドを一つずつvolatileとして読み込む
                out_ce.dw0 = ce->dw0;
                out_ce.dw1 = ce->dw1;
                out_ce.sq_head = ce->sq_head;
                out_ce.sq_id = ce->sq_id;
                out_ce.cid = ce->cid;
                out_ce.status = st; // 最初に読んだstを最後に入れる

                // グローバルな状態変数を更新
                g.io_cq_head = (uint16_t)((g.io_cq_head + 1) % g.io_qsize);
                if (g.io_cq_head == 0)
                    g.io_cq_phase ^= 1;

                // ドアベルを更新してコントローラに通知
                *doorbell_cq(1) = g.io_cq_head;

                out_status = (uint16_t)(st >> 1);
                return true;
            }
        }
        con.println("NVMe: IO completion timeout");
        return false;
    }

    static uint32_t cap_mqes(uint64_t cap) { return (uint32_t)(cap & 0xFFFFull); }
    static uint32_t cap_dstrd(uint64_t cap) { return (uint32_t)((cap >> 32) & 0xFull); }
    static uint32_t cap_to(uint64_t cap) { return (uint32_t)((cap >> 24) & 0xFFull); } // 単位は実装依存で余裕を持って使う

    static inline void mfence() { asm volatile("mfence" ::: "memory"); }

    static inline void wr64_split(volatile uint32_t &lo, volatile uint32_t &hi, uint64_t v)
    {
        lo = (uint32_t)(v & 0xFFFFFFFFu);
        hi = (uint32_t)(v >> 32);
    }

    static inline uint64_t rd64_split(volatile uint32_t &lo, volatile uint32_t &hi)
    {
        uint32_t hi1 = hi; // 先に上位
        uint32_t lo1 = lo;
        uint32_t hi2 = hi; // 破断対策で読みなおし
        if (hi1 != hi2)
        { // たまにずれるなら取り直す
            lo1 = lo;
            hi1 = hi2;
        }
        return ((uint64_t)hi1 << 32) | lo1;
    }

    static bool alloc_dma32_page(void **out_va, uint64_t &out_pa, Console &con)
    {
        for (int t = 0; t < 32; ++t)
        {
            void *va = pmm::alloc_pages(1);
            con.printf("Debug: pmm::alloc_pages(1) returned physical address: %p\n", va);
            if (!va)
                return false;
            uint64_t pa = paging::virt_to_phys((uint64_t)(uintptr_t)va);
            con.printf("Debug: virt_to_phys(%p) returned: %p\n", va, (void *)pa);
            if (pa == (uint64_t)-1)
            {
                con.printf("NVMe Error: virt_to_phys failed for VA %p\n", va);
                return false;
            }
            if (((pa & 0xFFF) == 0) && (pa < (1ull << 32)))
            {
                *out_va = va;
                out_pa = pa;
                return true;
            }
            // 低位が取れなかった。free_pages があれば解放する（無ければ諦めて次）。
            pmm::free_pages(va, 1);  // ←APIがあれば使う
        }
        return false;
    }

    static bool admin_wait_complete(Console &con, uint16_t &out_status, CqEntry &out_ce)
    {
        for (uint32_t spin = 0; spin < 10000000; ++spin)
        {
            const volatile CqEntry *ce = &g.acq[g.cq_head];

            // volatileなstatusを読み込む
            uint16_t st = ce->status;

            if ((st & 1) == g.cq_phase)
            {
                // volatileな完了エントリの内容を安全にコピー
                out_ce.dw0 = ce->dw0;
                out_ce.dw1 = ce->dw1;
                out_ce.sq_head = ce->sq_head;
                out_ce.sq_id = ce->sq_id;
                out_ce.cid = ce->cid;
                out_ce.status = st;

                // CPUによる状態変数の更新と、デバイスに見えるMMIO書き込みの順序を保証する
                // ために、sfence (ストアフェンス) をここに配置する
                asm volatile("sfence" ::: "memory");

                // グローバルな状態変数を更新
                g.cq_head = (uint16_t)((g.cq_head + 1) % g.qsize);
                if (g.cq_head == 0)
                    g.cq_phase ^= 1;

                // ドアベルを更新してコントローラに通知
                *doorbell_cq(0) = g.cq_head;

                out_status = (uint16_t)(st >> 1);
                return true;
            }
        }
        con.println("NVMe: admin completion timeout");
        return false;
    }

    // qsize を安全な値に丸める（SQ容量=4096/64=64, CQ容量=4096/16=256, CAP.MQES+1 以下）
    static uint16_t clamp_io_qsize(uint16_t want)
    {
        uint16_t max_by_page_sq = 64;
        uint16_t max_by_page_cq = 256;
        uint16_t max_by_cap = (uint16_t)(cap_mqes(g.cap_cache) + 1);
        uint16_t q = want;
        if (q > max_by_page_sq)
            q = max_by_page_sq;
        if (q > max_by_page_cq)
            q = max_by_page_cq;
        if (q > max_by_cap)
            q = max_by_cap;
        if (q < 2)
            q = 2;
        return q;
    }

    static bool set_features_num_queues(uint16_t nsq_pairs_minus1, uint16_t ncq_pairs_minus1, Console &con)
    {
        SqEntry cmd{};
        bzero(&cmd, sizeof(cmd));
        cmd.opc = 0x09; // Set Features (Admin)
        cmd.cid = 7;
        cmd.cdw10 = 0x07; // FID = Number of Queues
        cmd.cdw11 = ((uint32_t)nsq_pairs_minus1 << 16) | (uint32_t)ncq_pairs_minus1;

        // 投入
        uint16_t slot = g.sq_tail;
        g.asq[slot] = cmd;
        asm volatile("sfence" ::: "memory");
        g.sq_tail = (uint16_t)((g.sq_tail + 1) % g.qsize);
        *doorbell_sq(0) = g.sq_tail;

        uint16_t st = 0;
        CqEntry ce{};
        if (!admin_wait_complete(con, st, ce))
            return false;
        bzero((void *)&g.acq[(g.cq_head + g.qsize - 1) % g.qsize], sizeof(CqEntry));
        if (st != 0)
        {
            con.printf("  CE.dw0=%08x dw1=%08x sqid=%u cid=%u\n",
                       (unsigned)ce.dw0, (unsigned)ce.dw1,
                       (unsigned)ce.sq_id, (unsigned)ce.cid);
            dump_nvme_status(con, st);
            con.printf("NVMe: Set Features(Number of Queues) failed, status=%04x\n", (unsigned)st);
            return false;
        }

        // 完了DW0に実反映された値（0-based）が入る
        uint16_t ncqr0 = (uint16_t)(ce.dw0 & 0xFFFFu);         // 0-based
        uint16_t nsqr0 = (uint16_t)((ce.dw0 >> 16) & 0xFFFFu); // 0-based
        g.ncqr = (uint16_t)(ncqr0 + 1);
        g.nsqr = (uint16_t)(nsqr0 + 1);

        con.printf("NVMe: NumberOfQueues result: NSQR=%u NCQR=%u\n",
                   (unsigned)g.nsqr, (unsigned)g.ncqr);
        con.printf("NVMe: Set Features NumberOfQueues OK (requested NSQR=%u, NCQR=%u)\n",
                   (unsigned)(nsq_pairs_minus1 + 1), (unsigned)(ncq_pairs_minus1 + 1));
        return true;
    }

    // --- Create IOCQ (qid=1) ---
    static bool create_iocq_q1(uint16_t qid, uint16_t qsize, Console &con)
    {
        if (!alloc_dma32_page((void **)&g.io_cq, g.io_cq_phys, con))
        {
            con.println("NVMe: alloc IOCQ DMA32 page failed");
            return false;
        }
        bzero(g.io_cq, 4096);
        con.printf("DBG: IOCQ PRP=%p qsize=%u\n", (void *)(uintptr_t)g.io_cq_phys, (unsigned)qsize);

        auto submit = [&](uint32_t cdw10) -> uint16_t
        {
            SqEntry cmd{};
            bzero(&cmd, sizeof(cmd));
            cmd.opc = 0x05;
            cmd.cid = 10;
            cmd.prp1 = g.io_cq_phys;
            cmd.cdw10 = cdw10;
            cmd.cdw11 = 0;
            cmd.cdw11 |= (1u << 0); // PC=1
            // First try with IEN=0
            uint32_t cdw11_first = cmd.cdw11;
            // Fallback pattern with IEN=1
            uint32_t cdw11_fallback = cmd.cdw11 | (1u << 1);
            uint16_t slot = g.sq_tail;
            g.asq[slot] = cmd;
            asm volatile("sfence" ::: "memory");
            g.sq_tail = (uint16_t)((g.sq_tail + 1) % g.qsize);
            *doorbell_sq(0) = g.sq_tail;
            uint16_t st = 0;
            CqEntry ce{};
            if (!admin_wait_complete(con, st, ce))
                return (uint16_t)0xFFFF;
            con.printf("IOCQ cdw10=%x cdw11=%x (qid=%u qsize=%u)\n",
                       (unsigned)cdw10, (unsigned)cmd.cdw11, (unsigned)qid, (unsigned)qsize);

            if (st != 0)
            {
                con.printf("NVMe: Create IOCQ failed, status=%x (sqid=%u cid=%u)\n",
                           (unsigned)st, (unsigned)ce.sq_id, (unsigned)ce.cid);
                dump_nvme_status(con, st);
                con.printf("  CE.dw0=%x dw1=%x  CSTS=%x\n",
                           (unsigned)ce.dw0, (unsigned)ce.dw1, (unsigned)g.r->CSTS);
            }
            return st;
        };

        // 1回目（通常順: [31:16]=QSIZE-1, [15:0]=QID）
        uint32_t cdw10_norm = ((uint32_t)(qsize - 1) << 16) | (uint32_t)qid;
        uint16_t st = submit(cdw10_norm);
        if (st == 0)
        {
            g.io_cq_head = 0;
            g.io_cq_phase = 1;
            *doorbell_cq(1) = 0;
            con.printf("NVMe: Create IOCQ qid=%u qsize=%u PRP=%p -> OK\n",
                       (unsigned)qid, (unsigned)qsize, (void *)(uintptr_t)g.io_cq_phys);
            return true;
        }

        // “Invalid Field” のときだけ順序を入れ替えて再試行（切り分け用）
        uint8_t sc = st & 0xFF;
        uint8_t sct = (st >> 8) & 0x7;
        if (sc == 0x02)
        {
            con.printf("NVMe: retry Create IOCQ with swapped CDW10 fields\n");
            uint32_t cdw10_swap = ((uint32_t)qid << 16) | (uint32_t)(qsize - 1);
            st = submit(cdw10_swap);
            if (st == 0)
            {
                g.io_cq_head = 0;
                g.io_cq_phase = 1;
                *doorbell_cq(1) = 0;
                con.printf("NVMe: Create IOCQ (swapped) qid=%u qsize=%u -> OK\n",
                           (unsigned)qid, (unsigned)qsize);
                return true;
            }
        }

        return false;
    }

    // --- Create IOSQ (qid=1) ---
    static bool create_iosq_q1(uint16_t qid, uint16_t qsize, Console &con)
    {
        if (!alloc_dma32_page((void **)&g.io_sq, g.io_sq_phys, con))
        {
            con.println("NVMe: alloc IOSQ DMA32 page failed");
            return false;
        }
        bzero(g.io_sq, 4096);
        g.io_sq_tail = 0;
        con.printf("DBG: IOSQ PRP=0x%p qsize=%u\n",
                   (void *)(uintptr_t)g.io_sq_phys, (unsigned)qsize);

        SqEntry cmd{};
        bzero(&cmd, sizeof(cmd));
        cmd.opc = 0x01;
        cmd.cid = 11;
        cmd.prp1 = g.io_sq_phys;
        cmd.cdw10 = 0;
        cmd.cdw10 |= ((uint32_t)(qsize - 1) << 16); // QSIZE-1
        cmd.cdw10 |= (uint32_t)qid;                 // QID
        cmd.cdw11 = 0;
        cmd.cdw11 |= (1u << 0);             // PC=1
        /* QPRIO=0 */                       // bits 2:1
        cmd.cdw11 |= ((uint32_t)qid << 16); // CQID

        uint16_t slot = g.sq_tail;
        g.asq[slot] = cmd;
        asm volatile("sfence" ::: "memory");
        g.sq_tail = (uint16_t)((g.sq_tail + 1) % g.qsize);
        *doorbell_sq(0) = g.sq_tail;

        uint16_t st = 0;
        CqEntry ce{};
        if (!admin_wait_complete(con, st, ce))
            return false;
        bzero((void *)&g.acq[(g.cq_head + g.qsize - 1) % g.qsize], sizeof(CqEntry));
        if (st != 0)
        {
            con.printf("  CE.dw0=%x dw1=%x sqid=%u cid=%u\n",
                       (unsigned)ce.dw0, (unsigned)ce.dw1,
                       (unsigned)ce.sq_id, (unsigned)ce.cid);
            dump_nvme_status(con, st);
            con.printf("NVMe: Create IOSQ failed, status=%x (sqid=%u cid=%u)\n",
                       (unsigned)st, (unsigned)ce.sq_id, (unsigned)ce.cid);
            return false;
        }

        *doorbell_sq(1) = 0;
        con.printf("NVMe: Create IOSQ qid=1 qsize=%u PRP=0x%p -> OK\n",
                   (unsigned)qsize, (void *)(uintptr_t)g.io_sq_phys);
        return true;
    }

    // ヘルパ（上に追加）
    static void try_delete_ioq(uint16_t qid, Console &con)
    {
        // Delete IOSQ (0x00)
        {
            SqEntry cmd{};
            bzero(&cmd, sizeof(cmd));
            cmd.opc = 0x00;
            cmd.cid = 0x20;
            cmd.cdw10 = qid;
            uint16_t slot = g.sq_tail;
            g.asq[slot] = cmd;
            asm volatile("sfence" ::: "memory");
            g.sq_tail = (uint16_t)((g.sq_tail + 1) % g.qsize);
            *doorbell_sq(0) = g.sq_tail;
            uint16_t st = 0;
            CqEntry ce{};
            admin_wait_complete(con, st, ce);
        }
        // Delete IOCQ (0x04)
        {
            SqEntry cmd{};
            bzero(&cmd, sizeof(cmd));
            cmd.opc = 0x04;
            cmd.cid = 0x21;
            cmd.cdw10 = qid;
            uint16_t slot = g.sq_tail;
            g.asq[slot] = cmd;
            asm volatile("sfence" ::: "memory");
            g.sq_tail = (uint16_t)((g.sq_tail + 1) % g.qsize);
            *doorbell_sq(0) = g.sq_tail;
            uint16_t st = 0;
            CqEntry ce{};
            admin_wait_complete(con, st, ce);
        }
    }

    bool create_io_queues(Console &con, uint16_t want_qsize)
    {
        if (!g.r)
        {
            con.println("NVMe: BAR0 not mapped");
            return false;
        }

        if (!set_features_num_queues(0, 0, con))
            return false;

        tiny_pause();

        g.r->INTMS = 0xFFFFFFFFu;
        g.r->INTMC = 0xFFFFFFFFu;

        if (g.nsqr == 0 || g.ncqr == 0)
        {
            con.printf("NVMe: controller reports no IO queues available (NSQR=%u, NCQR=%u)\n",
                       (unsigned)g.nsqr, (unsigned)g.ncqr);
            return false;
        }

        uint16_t q = clamp_io_qsize(want_qsize);
        g.io_qsize = q;

        // 探索範囲（1..min(NSQR,NCQR)）。上限は過剰トライを避けて 8 に丸める
        uint16_t max_qid_all = (g.nsqr < g.ncqr) ? g.nsqr : g.ncqr;
        if (max_qid_all > 8)
            max_qid_all = 8;

        for (uint16_t qid = 1; qid <= max_qid_all; ++qid)
        {
            con.printf("NVMe: creating IO queues (try qid=%u) use=%u\n",
                       (unsigned)qid, (unsigned)q);
            try_delete_ioq(qid, con);
            if (!create_iocq_q1(qid, q, con))
            {
                con.printf("NVMe: qid=%u IOCQ create failed, trying next qid...\n",
                           (unsigned)qid);
                continue; // 次の QID を試す
            }
            if (!create_iosq_q1(qid, q, con))
            {
                con.printf("NVMe: qid=%u IOSQ create failed, trying next qid...\n",
                           (unsigned)qid);
                // （必要なら Delete IOCQ を入れるが、いまは失敗時に次を試すだけ）
                continue;
            }

            // 成功
            con.printf("NVMe IO DB stride=%u  SQ%u@%p  CQ%u@%p\n",
                       (unsigned)g.db_stride, (unsigned)qid, doorbell_sq(qid),
                       (unsigned)qid, doorbell_cq(qid));
            return true;
        }

        con.println("NVMe: failed to create any IO queue (tried qid 1..N)");
        return false;
    }

    static bool identify_controller(Console &con)
    {
        // 4KiB データバッファ
        void *buf = pmm::alloc_pages(1);
        if (!buf)
        {
            con.println("NVMe: PRP buffer alloc failed");
            return false;
        }
        bzero(buf, 4096);

        uint64_t prp1_phys = paging::virt_to_phys((uintptr_t)buf);

        // SQエントリ作成
        SqEntry cmd{};
        bzero(&cmd, sizeof(cmd));
        cmd.opc = 0x06; // Identify
        cmd.cid = 1;    // 任意のCID
        cmd.nsid = 0;   // Identify Controller
        cmd.prp1 = prp1_phys;
        cmd.cdw10 = 1; // CNS=1 (controller)

        // SQに投入
        uint16_t slot = g.sq_tail;
        g.asq[slot] = cmd;
        asm volatile("sfence" ::: "memory");
        // ★ デバイスから見えるようにフェンス（x86 では通常不要だが安全策）
        g.sq_tail = (uint16_t)((g.sq_tail + 1) % g.qsize);
        *doorbell_sq(0) = g.sq_tail; // SQ tail doorbell
        con.printf("DBG: DB(SQ0)=%u (addr=%p)\n",
                   (unsigned)g.sq_tail, doorbell_sq(0));

        uint16_t st = 0;
        CqEntry ce{};
        if (!admin_wait_complete(con, st, ce))
        {
            con.println("NVMe: Identify timeout");
            return false;
        }
        if (st != 0)
        {
            con.printf("NVMe: Identify failed, status=%04x\n", (unsigned)st);
            return false;
        }

        // 成功後の処理
        uint8_t *ctrl = (uint8_t *)buf;
        uint8_t mdts_val = ctrl[77];

        // 現在プログラム済みのページサイズ (CC.MPS) から MPS バイト数を算出
        uint32_t cc_rb = g.r->CC;                  // すでに有効化後
        uint32_t mps_exp = (cc_rb >> 7) & 0xF;     // CC.MPS = log2(bytes) - 12
        uint32_t mps_bytes = 1u << (12 + mps_exp); // 例: MPS=0 -> 4096B

        uint64_t max_bytes64;
        if (mdts_val == 0)
        {
            // MDTS=0 は“制限なし”扱いで運用（実機相性のため大きめ許容）
            max_bytes64 = 0xFFFFFFFFull;
        }
        else
        {
            // max = MPS * 2^MDTS
            max_bytes64 = ((uint64_t)mps_bytes) << mdts_val;
            if (max_bytes64 > 0xFFFFFFFFull)
                max_bytes64 = 0xFFFFFFFFull;
        }
        g.mdts = mdts_val;
        g.max_xfer_bytes = (uint32_t)max_bytes64;
        con.println("NVMe: Identify Controller OK");
        return true;
    }

    static bool identify_namespace(uint32_t nsid, Console &con)
    {
        // 4KiB バッファ確保（PRP1 で渡す）
        void *buf = pmm::alloc_pages(1);
        if (!buf)
        {
            con.println("NVMe: PRP buffer alloc failed (NS)");
            return false;
        }
        bzero(buf, 4096);
        uint64_t prp1_phys = paging::virt_to_phys((uintptr_t)buf);

        // SQエントリ
        SqEntry cmd{};
        bzero(&cmd, sizeof(cmd));
        cmd.opc = 0x06;  // Identify (Admin)
        cmd.cid = 2;     // 任意のCID
        cmd.nsid = nsid; // 取得対象 NS
        cmd.prp1 = prp1_phys;
        cmd.cdw10 = 0x00; // CNS=0x00 -> Identify Namespace  :contentReference[oaicite:1]{index=1}

        // SQ投入
        uint16_t slot = g.sq_tail;
        g.asq[slot] = cmd;
        asm volatile("sfence" ::: "memory");
        g.sq_tail = (uint16_t)((g.sq_tail + 1) % g.qsize);
        *doorbell_sq(0) = g.sq_tail; // SQ tail doorbell

        uint16_t st = 0;
        CqEntry ce{};
        if (!admin_wait_complete(con, st, ce))
        {
            con.println("NVMe: Identify timeout");
            return false;
        }
        if (st != 0)
        {
            con.printf("NVMe: Identify failed, status=%04x\n", (unsigned)st);
            return false;
        }

        // 成功後の処理
        const NvmeIdentifyNamespace *ns =
            reinterpret_cast<const NvmeIdentifyNamespace *>(buf);

        // 使用 LBAF index とセクタサイズ算出（FLBAS[3:0] = idx, LBADS = log2(bytes)）
        uint8_t idx = (uint8_t)(ns->flbas & 0x0F);
        uint8_t max = (uint8_t)(ns->nlbaf & 0x1F); // 仕様上「数-1」
        if (idx > max)
        {
            con.printf("NVMe: FLBAS index %u > NLBAF %u, fallback to 0\n",
                       (unsigned)idx, (unsigned)max);
            idx = 0;
        }
        uint32_t sector_size = 1u << ns->lbaf[idx].lbads; // bytes
        if (sector_size == 0)
            sector_size = 512;

        g.ns_active = nsid;
        g.lba_bytes = sector_size;

        // 容量換算（LBA数 × セクタサイズ）
        auto mul64_clamp = [](uint64_t a, uint64_t b) -> __uint128_t
        {
            return ((__uint128_t)a) * ((__uint128_t)b);
        };
        __uint128_t nsze_bytes = mul64_clamp(ns->nsze, sector_size);
        __uint128_t ncap_bytes = mul64_clamp(ns->ncap, sector_size);
        __uint128_t nuse_bytes = mul64_clamp(ns->nuse, sector_size);

        // ログ出力（128bit は下位64bitのみを十進で出す簡易表示）
        auto lo64 = [](__uint128_t x) -> uint64_t
        { return (uint64_t)x; };
        con.printf("NVMe: Identify Namespace #%u OK\n", (unsigned)nsid);
        con.printf("  Sector size : %u bytes  (LBAF=%u, LBADS=%u)\n",
                   (unsigned)sector_size, (unsigned)idx, (unsigned)ns->lbaf[idx].lbads);
        con.printf("  NSZE : %llu LBAs  (%llu bytes)\n",
                   (unsigned long long)ns->nsze,
                   (unsigned long long)lo64(nsze_bytes));
        con.printf("  NCAP : %llu LBAs  (%llu bytes)\n",
                   (unsigned long long)ns->ncap,
                   (unsigned long long)lo64(ncap_bytes));
        con.printf("  NUSE : %llu LBAs  (%llu bytes)\n",
                   (unsigned long long)ns->nuse,
                   (unsigned long long)lo64(nuse_bytes));
        con.println("NVMe: Identify Namespace OK");
        return true;
    }

    bool init(void *bar0_va, Console &con)
    {
        g.r = (volatile NvmeRegs *)bar0_va;

        con.printf("NVMe BAR0 VA=%p\n", (void *)g.r);

        g.cap_cache = g.r->CAP;
        g.vs_cache = g.r->VS;

        g.db_stride = 4u << cap_dstrd(g.cap_cache);
        uint32_t to = cap_to(g.cap_cache);
        if (to == 0)
            to = 10;

        // 1) 停止
        g.r->CC &= ~1u;
        if (!wait_rdy(0, to * 200000))
        {
            con.println("NVMe: disable timeout");
            return false;
        }

        // 2) Admin Queue 確保（各 4KiB/32 entries）
        uint16_t max_entries = (uint16_t)(cap_mqes(g.cap_cache) + 1);
        g.qsize = (max_entries >= 32) ? 32 : (max_entries >= 2 ? max_entries : 2);

        g.asq = (SqEntry *)pmm::alloc_pages(1);
        g.acq = (CqEntry *)pmm::alloc_pages(1);
        if (!g.asq || !g.acq)
        {
            con.println("NVMe: admin queue alloc failed");
            return false;
        }
        bzero(g.asq, 4096);
        bzero(g.acq, 4096);
        g.sq_tail = 0;
        g.cq_head = 0;
        g.cq_phase = 1;

        uint64_t asq_phys = paging::virt_to_phys((uint64_t)(uintptr_t)g.asq);
        uint64_t acq_phys = paging::virt_to_phys((uint64_t)(uintptr_t)g.acq);
        if ((int64_t)asq_phys < 0 || (int64_t)acq_phys < 0)
        {
            con.println("NVMe: virt_to_phys failed for admin queues");
            return false;
        }
        // 4KiB アライン前提のチェック（念のため）
        if ((asq_phys & 0xFFF) || (acq_phys & 0xFFF))
        {
            con.printf("NVMe: ASQ/ACQ not 4K aligned: ASQ=%p ACQ=%p\n",
                       (void *)(uintptr_t)asq_phys, (void *)(uintptr_t)acq_phys);
            return false;
        }

        // 3) AQA / ASQ / ACQ
        g.r->AQA = ((uint32_t)(g.qsize - 1) << 16) | (uint32_t)(g.qsize - 1);
        wr64_split(g.r->ASQ_LO, g.r->ASQ_HI, asq_phys);
        wr64_split(g.r->ACQ_LO, g.r->ACQ_HI, acq_phys);
        asm volatile("" ::: "memory");

        uint32_t aqa_rb = g.r->AQA;
        uint64_t asq_rb = rd64_split(g.r->ASQ_LO, g.r->ASQ_HI);
        uint64_t acq_rb = rd64_split(g.r->ACQ_LO, g.r->ACQ_HI);
        con.printf("NVMe AQA=%08x  ASQ(phys)=0x%p  ACQ(phys)=0x%p\n",
                   aqa_rb, (void *)(uintptr_t)asq_rb, (void *)(uintptr_t)acq_rb);
        if (asq_rb == 0 || acq_rb == 0)
        {
            con.println("NVMe: ASQ/ACQ readback is zero -> abort");
            return false;
        }

        con.printf("NVMe DB stride=%u  SQ0@%p  CQ0@%p\n",
                   (unsigned)g.db_stride, doorbell_sq(0), doorbell_cq(0));

        *doorbell_sq(0) = 0;
        *doorbell_cq(0) = 0;

        // 4) CC: MPS=0(4KiB), CSS=0(NVM), AMS=0, EN=1
        uint32_t cc = g.r->CC;
        cc &= ~((0xFu << 20) | (0xFu << 16) | (0xFu << 7) | (0x7u << 4));
        cc |= (4u << 20); // IOCQES = log2(16) = 4
        cc |= (6u << 16); // IOSQES = log2(64) = 6
        cc |= (0u << 7);  // MPS = log2(4096)-12 = 0
        cc |= (0u << 4);  // CSS = 0 (NVM Command Set)
        g.r->CC = cc;
        asm volatile("" ::: "memory");
        g.r->CC = cc | 1u;

        if (!wait_rdy(1, to * 200000))
        {
            con.println("NVMe: enable timeout");
            return false;
        }
        con.printf("NVMe: admin queues ready (Q=%u, DSTRD=%u)\n",
                   (unsigned)g.qsize, (unsigned)cap_dstrd(g.cap_cache));

        con.printf("NVMe DB stride=%u  SQ0@%p  CQ0@%p\n",
                   (unsigned)g.db_stride, doorbell_sq(0), doorbell_cq(0));

        // 5) Identify Controller を1発
        (void)identify_controller(con);
        (void)identify_namespace(1, con);
        return true;
    }

    bool init_and_create_queues(void *bar0_va, Console &con, uint16_t want_qsize)
    {
        g.r = (volatile NvmeRegs *)bar0_va;
        g.cap_cache = g.r->CAP;
        g.db_stride = 4u << ((g.cap_cache >> 32) & 0xF);

        // --- ステップ1: コントローラを無効化 ---
        g.r->CC &= ~1u;
        for (uint32_t i = 0; i < 100000; ++i)
        {
            if ((g.r->CSTS & 1u) == 0)
                break;
        }
        if ((g.r->CSTS & 1u) != 0)
        {
            con.println("NVMe: disable timeout");
            return false;
        }

        // --- ステップ2: Admin Queueを設定 ---
        g.qsize = 32;
        g.asq = (SqEntry *)pmm::alloc_pages(1);
        g.acq = (CqEntry *)pmm::alloc_pages(1);
        if (!g.asq || !g.acq)
            return false;
        bzero(g.asq, 4096);
        bzero(g.acq, 4096);
        g.sq_tail = 0;
        g.cq_head = 0;
        g.cq_phase = 1;

        uint64_t asq_phys = paging::virt_to_phys((uint64_t)(uintptr_t)g.asq);
        uint64_t acq_phys = paging::virt_to_phys((uint64_t)(uintptr_t)g.acq);
        if (asq_phys == (uint64_t)-1 || acq_phys == (uint64_t)-1)
            return false;

        g.r->AQA = ((uint32_t)(g.qsize - 1) << 16) | (uint32_t)(g.qsize - 1);
        g.r->ASQ_LO = (uint32_t)asq_phys;
        g.r->ASQ_HI = (uint32_t)(asq_phys >> 32);
        g.r->ACQ_LO = (uint32_t)acq_phys;
        g.r->ACQ_HI = (uint32_t)(acq_phys >> 32);

        // --- ステップ3: コントローラ設定(CC)を書き込み、有効化する ---
        uint32_t cc = (4u << 20) | (6u << 16) | (0u << 7) | (0u << 4);
        g.r->CC = cc;
        asm volatile("sfence" ::: "memory");
        g.r->CC = cc | 1u; // EN=1
        for (uint32_t i = 0; i < 100000; ++i)
        {
            if ((g.r->CSTS & 1u) == 1)
                break;
        }
        if ((g.r->CSTS & 1u) != 1)
        {
            con.println("NVMe: enable timeout");
            return false;
        }
        con.println("NVMe: Admin queues ready.");

        // --- ステップ4: AdminコマンドでコントローラとNSを識別 ---
        if (!identify_controller(con))
            return false;
        if (!identify_namespace(1, con))
            return false;

        // --- ステップ5: AdminコマンドでI/Oキューの数を設定 ---
        if (!set_features_num_queues(0, 0, con))
            return false;

        // --- ステップ6: I/Oキューを作成 ---
        uint16_t q = clamp_io_qsize(want_qsize);
        g.io_qsize = q;
        if (!create_iocq_q1(1, q, con))
            return false;
        if (!create_iosq_q1(1, q, con))
            return false;

        con.printf("NVMe: I/O queues (QID=1, QSIZE=%u) created successfully.\n", (unsigned)q);
        return true;
    }

    bool flush(uint32_t nsid, Console &con)
    {
        if (!g.io_sq || !g.io_cq || g.io_qsize == 0)
        {
            con.println("NVMe: IO queues not ready");
            return false;
        }

        // SQE 構築（NVM FLUSH はデータバッファ不要）
        SqEntry cmd{};
        bzero(&cmd, sizeof(cmd));
        cmd.opc = 0x00; // NVM FLUSH
        cmd.fuse = 0;
        cmd.cid = (uint16_t)(g.io_sq_tail & 0xFFFF);
        cmd.nsid = nsid;
        cmd.mptr = 0;
        cmd.prp1 = 0;
        cmd.prp2 = 0;
        cmd.cdw10 = 0;
        cmd.cdw11 = 0;
        cmd.cdw12 = 0;
        cmd.cdw13 = 0;
        cmd.cdw14 = 0;
        cmd.cdw15 = 0;

        // 投入
        uint16_t slot = (uint16_t)(g.io_sq_tail % g.io_qsize);
        g.io_sq[slot] = cmd;
        asm volatile("sfence" ::: "memory");
        g.io_sq_tail = (uint16_t)((g.io_sq_tail + 1) % g.io_qsize);
        *doorbell_sq(1) = g.io_sq_tail;

        // 完了待ち
        uint16_t st = 0;
        CqEntry ce{};
        if (!io_wait_complete(con, st, ce))
            return false;

        if (st != 0)
        {
            dump_nvme_status(con, st);
            con.printf("NVMe: FLUSH failed (SQID=%u CID=%u)\n",
                       (unsigned)ce.sq_id, (unsigned)ce.cid);
            return false;
        }

        con.printf("NVMe: FLUSH OK (nsid=%u)\n", (unsigned)nsid);
        return true;
    }

    bool read_lba(uint32_t nsid, uint64_t slba, uint16_t nlb,
                  void *buf, size_t buf_bytes, Console &con)
    {
        if (!g.io_sq || !g.io_cq || g.io_qsize == 0)
        {
            con.println("NVMe: IO queues not ready");
            return false;
        }
        if (nlb == 0)
        {
            con.println("NVMe: read_lba: nlb must be >= 1");
            return false;
        }

        // 物理アドレス取得と1ページ内に収まることの確認（最初はPRP1のみ対応）
        uint64_t prp1 = paging::virt_to_phys((uint64_t)(uintptr_t)buf);
        if ((prp1 & 0xFFFFFFFF00000000ull) != 0)
        {
            con.println("NVMe: PRP1 must be DMA32 (<4GiB) for now");
            return false;
        }
        const size_t total_bytes = (size_t)nlb * (size_t)g.lba_bytes;
        if (total_bytes > buf_bytes)
        {
            con.printf("NVMe: buffer too small (need %u)\n", (unsigned)total_bytes);
            return false;
        }

        uint32_t maxb = g.max_xfer_bytes ? g.max_xfer_bytes : 0xFFFFFFFFu;
        size_t max_nlb_per_cmd = (size_t)(maxb / g.lba_bytes);
        if (max_nlb_per_cmd == 0)
            max_nlb_per_cmd = 1; // safety

        size_t remain_nlb = nlb;
        uint8_t *cursor = (uint8_t *)buf;
        uint64_t curr_slba = slba;
        unsigned chunks = 0;

        auto issue_chunk = [&](uint16_t this_nlb, void *this_buf, size_t this_bytes) -> bool
        {
            // --- 以降は従来の PRP/PRP2/PRP List 構築と投入・完了待ちを、そのまま this_* を使って実行 ---
            uint64_t prp1 = paging::virt_to_phys((uint64_t)(uintptr_t)this_buf);
            if ((prp1 & 0xFFFFFFFF00000000ull) != 0)
            {
                con.println("NVMe: PRP1 must be DMA32 (<4GiB) for now");
                return false;
            }

            const uintptr_t va = (uintptr_t)this_buf;
            const uintptr_t va_p0 = va & ~0xFFFULL;
            const size_t off_p0 = (size_t)(va & 0xFFFULL);
            const size_t room_p0 = 4096 - off_p0;
            size_t remain = (this_bytes > room_p0) ? (this_bytes - room_p0) : 0;

            uint64_t prp2 = 0;
            void *list_cur_va = nullptr;
            uint64_t list_cur_pa = 0;
            uint64_t *list_ptr = nullptr;
            size_t list_idx = 0;
            size_t pages_left = (remain + 4095) / 4096;

            auto alloc_new_prp_list_page = [&]() -> bool
            {
                if (!alloc_dma32_page(&list_cur_va, list_cur_pa, con))
                    return false;
                bzero(list_cur_va, 4096);
                list_ptr = (uint64_t *)list_cur_va;
                list_idx = 0;
                return true;
            };
            auto phys_of_page_va = [&](uintptr_t page_base_va) -> uint64_t
            {
                return paging::virt_to_phys((uint64_t)page_base_va);
            };

            if (pages_left == 0)
            {
                // PRP2 unused
            }
            else if (pages_left == 1)
            {
                uintptr_t va_p1 = va_p0 + 0x1000;
                uint64_t pa_p1 = phys_of_page_va(va_p1);
                if ((pa_p1 & 0xFFFFFFFF00000000ull) != 0)
                {
                    con.println("NVMe: PRP2 must be DMA32 (<4GiB) for now");
                    return false;
                }
                prp2 = pa_p1;
            }
            else
            {
                if (!alloc_new_prp_list_page())
                {
                    con.println("NVMe: alloc PRP List page failed");
                    return false;
                }
                prp2 = list_cur_pa;
                uintptr_t next_page_va = va_p0 + 0x1000;
                for (size_t i = 0; i < pages_left; ++i)
                {
                    if (list_idx == 511 && (i + 1) < pages_left)
                    {
                        void *nxt_va = nullptr;
                        uint64_t nxt_pa = 0;
                        if (!alloc_dma32_page(&nxt_va, nxt_pa, con))
                        {
                            con.println("NVMe: alloc next PRP List page failed");
                            return false;
                        }
                        bzero(nxt_va, 4096);
                        list_ptr[511] = nxt_pa; // chain
                        list_cur_va = nxt_va;
                        list_cur_pa = nxt_pa;
                        list_ptr = (uint64_t *)list_cur_va;
                        list_idx = 0;
                    }
                    uint64_t pa_pi = phys_of_page_va(next_page_va);
                    if ((pa_pi & 0xFFF) != 0)
                    {
                        con.println("NVMe: data page is not 4K aligned (unexpected)");
                        return false;
                    }
                    if ((pa_pi & 0xFFFFFFFF00000000ull) != 0)
                    {
                        con.println("NVMe: data page must be DMA32 (<4GiB) for now");
                        return false;
                    }
                    list_ptr[list_idx++] = pa_pi;
                    next_page_va += 0x1000;
                }
            }

            SqEntry cmd{};
            bzero(&cmd, sizeof(cmd));
            cmd.opc = 0x02;
            cmd.fuse = 0;
            cmd.cid = (uint16_t)(g.io_sq_tail & 0xFFFF);
            cmd.nsid = nsid;
            cmd.mptr = 0;
            cmd.prp1 = prp1;
            cmd.prp2 = prp2;
            cmd.cdw10 = (uint32_t)(curr_slba & 0xFFFFFFFFu);
            cmd.cdw11 = (uint32_t)(curr_slba >> 32);
            cmd.cdw12 = (uint32_t)((uint32_t)(this_nlb - 1) & 0xFFFFu);
            cmd.cdw13 = 0;
            cmd.cdw14 = 0;
            cmd.cdw15 = 0;

            uint16_t slot = (uint16_t)(g.io_sq_tail % g.io_qsize);
            g.io_sq[slot] = cmd;
            asm volatile("sfence" ::: "memory");
            g.io_sq_tail = (uint16_t)((g.io_sq_tail + 1) % g.io_qsize);
            *doorbell_sq(1) = g.io_sq_tail;

            uint16_t st = 0;
            CqEntry ce{};
            if (!io_wait_complete(con, st, ce))
                return false;
            if (st != 0)
            {
                dump_nvme_status(con, st);
                con.printf("NVMe: READ failed (SQID=%u CID=%u)\n", (unsigned)ce.sq_id, (unsigned)ce.cid);
                return false;
            }
            return true;
        };

        if (total_bytes > maxb)
        {
            size_t chunks_est = (total_bytes + maxb - 1) / maxb;
            con.printf("NVMe: MDTS split READ (%uB -> max %uB) chunks=%u\n",
                       (unsigned)total_bytes, (unsigned)maxb, (unsigned)chunks_est);
        }

        while (remain_nlb > 0)
        {
            uint16_t this_nlb = (uint16_t)((remain_nlb > max_nlb_per_cmd) ? max_nlb_per_cmd : remain_nlb);
            size_t this_bytes = (size_t)this_nlb * (size_t)g.lba_bytes;
            if (!issue_chunk(this_nlb, cursor, this_bytes))
                return false;
            cursor += this_bytes;
            curr_slba += this_nlb;
            remain_nlb -= this_nlb;
            ++chunks;
        }
        return true;
    }

    bool write_lba(uint32_t nsid, uint64_t slba, uint16_t nlb,
                   const void *buf, size_t buf_bytes, uint32_t flags, Console &con)
    {
        if (!g.io_sq || !g.io_cq || g.io_qsize == 0)
        {
            con.println("NVMe: IO queues not ready");
            return false;
        }
        if (nlb == 0)
        {
            con.println("NVMe: write_lba: nlb must be >= 1");
            return false;
        }

        // ---- PRP 準備（PRP1 / PRP2 / PRP List）----
        uintptr_t va = (uintptr_t)buf;
        uint64_t prp1 = paging::virt_to_phys((uint64_t)va);
        if ((prp1 & 0xFFFFFFFF00000000ull) != 0)
        {
            con.println("NVMe: PRP1 must be DMA32 (<4GiB) for now");
            return false;
        }
        const size_t total_bytes = (size_t)nlb * (size_t)g.lba_bytes;
        if (total_bytes > buf_bytes)
        {
            con.printf("NVMe: buffer too small (need %u)\n", (unsigned)total_bytes);
            return false;
        }

        uint32_t maxb = g.max_xfer_bytes ? g.max_xfer_bytes : 0xFFFFFFFFu;
        size_t max_nlb_per_cmd = (size_t)(maxb / g.lba_bytes);
        if (max_nlb_per_cmd == 0)
            max_nlb_per_cmd = 1;

        size_t remain_nlb = nlb;
        const uint8_t *cursor = (const uint8_t *)buf;
        uint64_t curr_slba = slba;
        unsigned chunks = 0;

        auto issue_chunk = [&](uint16_t this_nlb, const void *this_buf, size_t this_bytes, bool is_last) -> bool
        {
            uintptr_t va = (uintptr_t)this_buf;
            uint64_t prp1 = paging::virt_to_phys((uint64_t)va);
            if ((prp1 & 0xFFFFFFFF00000000ull) != 0)
            {
                con.println("NVMe: PRP1 must be DMA32 (<4GiB) for now");
                return false;
            }

            const uintptr_t va_p0 = va & ~0xFFFULL;
            const size_t off_p0 = (size_t)(va & 0xFFFULL);
            const size_t room_p0 = 4096 - off_p0;
            size_t remain = (this_bytes > room_p0) ? (this_bytes - room_p0) : 0;

            uint64_t prp2 = 0;
            void *list_cur_va = nullptr;
            uint64_t list_cur_pa = 0;
            uint64_t *list_ptr = nullptr;
            size_t list_idx = 0;
            size_t pages_left = (remain + 4095) / 4096;

            auto alloc_new_prp_list_page = [&]() -> bool
            {
                if (!alloc_dma32_page(&list_cur_va, list_cur_pa, con))
                    return false;
                bzero(list_cur_va, 4096);
                list_ptr = (uint64_t *)list_cur_va;
                list_idx = 0;
                return true;
            };
            auto phys_of_page_va = [&](uintptr_t page_base_va) -> uint64_t
            {
                return paging::virt_to_phys((uint64_t)page_base_va);
            };

            if (pages_left == 0)
            {
                // PRP2 unused
            }
            else if (pages_left == 1)
            {
                uintptr_t va_p1 = va_p0 + 0x1000;
                uint64_t pa_p1 = phys_of_page_va(va_p1);
                if ((pa_p1 & 0xFFFFFFFF00000000ull) != 0)
                {
                    con.println("NVMe: PRP2 must be DMA32 (<4GiB) for now");
                    return false;
                }
                prp2 = pa_p1;
            }
            else
            {
                if (!alloc_new_prp_list_page())
                {
                    con.println("NVMe: alloc PRP List page failed");
                    return false;
                }
                prp2 = list_cur_pa;
                uintptr_t next_page_va = va_p0 + 0x1000;
                for (size_t i = 0; i < pages_left; ++i)
                {
                    if (list_idx == 511 && (i + 1) < pages_left)
                    {
                        void *nxt_va = nullptr;
                        uint64_t nxt_pa = 0;
                        if (!alloc_dma32_page(&nxt_va, nxt_pa, con))
                        {
                            con.println("NVMe: alloc next PRP List page failed");
                            return false;
                        }
                        bzero(nxt_va, 4096);
                        list_ptr[511] = nxt_pa; // chain
                        list_cur_va = nxt_va;
                        list_cur_pa = nxt_pa;
                        list_ptr = (uint64_t *)list_cur_va;
                        list_idx = 0;
                    }
                    uint64_t pa_pi = phys_of_page_va(next_page_va);
                    if ((pa_pi & 0xFFF) != 0)
                    {
                        con.println("NVMe: data page is not 4K aligned (unexpected)");
                        return false;
                    }
                    if ((pa_pi & 0xFFFFFFFF00000000ull) != 0)
                    {
                        con.println("NVMe: data page must be DMA32 (<4GiB) for now");
                        return false;
                    }
                    list_ptr[list_idx++] = pa_pi;
                    next_page_va += 0x1000;
                }
            }

            SqEntry cmd{};
            bzero(&cmd, sizeof(cmd));
            cmd.opc = 0x01;
            cmd.fuse = 0;
            cmd.cid = (uint16_t)(g.io_sq_tail & 0xFFFF);
            cmd.nsid = nsid;
            cmd.mptr = 0;
            cmd.prp1 = prp1;
            cmd.prp2 = prp2;
            cmd.cdw10 = (uint32_t)(curr_slba & 0xFFFFFFFFu);
            cmd.cdw11 = (uint32_t)(curr_slba >> 32);
            cmd.cdw12 = (uint32_t)((uint32_t)(this_nlb - 1) & 0xFFFFu);
            cmd.cdw13 = 0;
            cmd.cdw14 = 0;
            cmd.cdw15 = 0;

            if ((flags & kWriteFua) && is_last)
                cmd.cdw12 |= (1u << 30);

            uint16_t slot = (uint16_t)(g.io_sq_tail % g.io_qsize);
            g.io_sq[slot] = cmd;
            asm volatile("sfence" ::: "memory");
            g.io_sq_tail = (uint16_t)((g.io_sq_tail + 1) % g.io_qsize);
            *doorbell_sq(1) = g.io_sq_tail;

            uint16_t st = 0;
            CqEntry ce{};
            if (!io_wait_complete(con, st, ce))
                return false;
            if (st != 0)
            {
                dump_nvme_status(con, st);
                con.printf("NVMe: WRITE failed (SQID=%u CID=%u)\n", (unsigned)ce.sq_id, (unsigned)ce.cid);
                return false;
            }
            return true;
        };

        if (total_bytes > maxb)
        {
            size_t chunks_est = (total_bytes + maxb - 1) / maxb;
            con.printf("NVMe: MDTS split WRITE (%uB -> max %uB) chunks=%u\n",
                       (unsigned)total_bytes, (unsigned)maxb, (unsigned)chunks_est);
        }

        while (remain_nlb > 0)
        {
            uint16_t this_nlb = (uint16_t)((remain_nlb > max_nlb_per_cmd) ? max_nlb_per_cmd : remain_nlb);
            size_t this_bytes = (size_t)this_nlb * (size_t)g.lba_bytes;
            bool is_last = (this_nlb == remain_nlb);
            if (!issue_chunk(this_nlb, cursor, this_bytes, is_last))
                return false;
            cursor += this_bytes;
            curr_slba += this_nlb;
            remain_nlb -= this_nlb;
            ++chunks;
        }
        return true;
    }

    uint64_t cap() { return g.cap_cache; }
    uint32_t vs() { return g.vs_cache; }
    uint32_t debug_read_vs()
    {
        if (!g.r)
            return 0xFFFFFFFF;
        return g.r->VS; // g.r ポインタ経由で直接ハードウェアレジスタを読む
    }

    uint32_t lba_bytes() { return g.lba_bytes; }

    bool debug_test_write_lba0(Console &con)
    {
        con.println("\n--- Running minimal WRITE test to LBA 0 ---");

        // 1. ページアラインされたバッファを確保
        void *buffer_va = pmm::alloc_pages(1);
        if (!buffer_va)
        {
            con.println("Minimal test: pmm::alloc_pages failed.");
            return false;
        }
        uint64_t buffer_pa = paging::virt_to_phys((uint64_t)(uintptr_t)buffer_va);
        if (buffer_pa == (uint64_t)-1)
        {
            con.println("Minimal test: virt_to_phys failed.");
            return false;
        }
        con.printf("Minimal test: Buffer VA=%p, PA=%p\n", buffer_va, (void *)buffer_pa);

        // バッファをテストパターンで埋める
        for (int i = 0; i < 512; ++i)
            ((uint8_t *)buffer_va)[i] = 0xAA;

        // 2. WRITEコマンドを構築 (bzeroに頼らず全フィールドを明示)
        SqEntry cmd{};
        cmd.opc = 0x01; // WRITE
        cmd.fuse = 0;
        cmd.cid = (uint16_t)(g.io_sq_tail & 0xFFFF);
        cmd.nsid = 1; // NameSpace 1 を想定
        cmd.rsv2 = 0;
        cmd.mptr = 0;
        cmd.prp1 = buffer_pa;
        cmd.prp2 = 0;  // 転送は1ページ内に収まるのでPRP2は0
        cmd.cdw10 = 0; // SLBA (lower 32bit) = 0
        cmd.cdw11 = 0; // SLBA (upper 32bit) = 0
        cmd.cdw12 = 0; // NLB = 0 (1ブロック転送)
        cmd.cdw13 = 0;
        cmd.cdw14 = 0;
        cmd.cdw15 = 0;

        // 3. コマンド投入
        con.println("Minimal test: Submitting WRITE command...");
        uint16_t slot = (uint16_t)(g.io_sq_tail % g.io_qsize);
        g.io_sq[slot] = cmd;
        asm volatile("sfence" ::: "memory");
        g.io_sq_tail = (uint16_t)((g.io_sq_tail + 1) % g.io_qsize);
        *doorbell_sq(1) = g.io_sq_tail;

        // 4. 完了待機
        uint16_t st = 0;
        CqEntry ce{};
        if (!io_wait_complete(con, st, ce))
        {
            con.println("Minimal test: Completion timeout.");
            return false;
        }

        if (st != 0)
        {
            dump_nvme_status(con, st);
            con.printf("Minimal test: WRITE FAILED (SQID=%u CID=%u)\n", (unsigned)ce.sq_id, (unsigned)ce.cid);
            return false;
        }

        con.println("\n--- Minimal WRITE test SUCCEEDED! ---\n");
        return flush(1, con);
    }

} // namespace nvme
