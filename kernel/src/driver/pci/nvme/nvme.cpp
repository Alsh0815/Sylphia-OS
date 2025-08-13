#include "nvme.hpp"
#include "nvme_regs.hpp"
#include "../../../paging.hpp"
#include "../../../pmm.hpp"
#include "../../../console.hpp"

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
    } g{};

    static inline void bzero(void *p, size_t n)
    {
        uint8_t *q = (uint8_t *)p;
        for (size_t i = 0; i < n; i++)
            q[i] = 0;
    }
    static inline void mmio_wmb() { asm volatile("" ::: "memory"); } // 必要十分の可視化

    static bool wait_rdy(int want, uint32_t loops)
    {
        for (uint32_t i = 0; i < loops; ++i)
        {
            if ((g.r->CSTS & 1u) == (uint32_t)want)
                return true;
        }
        return false;
    }
    static uint32_t cap_mqes(uint64_t cap) { return (uint32_t)(cap & 0xFFFFull); }
    static uint32_t cap_dstrd(uint64_t cap) { return (uint32_t)((cap >> 32) & 0xFull); }
    static uint32_t cap_to(uint64_t cap) { return (uint32_t)((cap >> 24) & 0xFFull); } // 単位は実装依存で余裕を持って使う

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

        // CQ待ち (超簡易ポーリング)
        for (uint32_t spin = 0; spin < 10000000; ++spin)
        {
            const volatile CqEntry *ce = &g.acq[g.cq_head];
            /*
            con.printf("DBG: CQH=%u CE.status=%x cid=%u sqid=%u head=%u\n",
                       (unsigned)g.cq_head, (unsigned)ce->status,
                       (unsigned)ce->cid, (unsigned)ce->sq_id, (unsigned)ce->sq_head);
            */
            uint16_t status = ce->status;
            if ((status & 1) == g.cq_phase)
            {
                // 完了
                g.cq_head = (uint16_t)((g.cq_head + 1) % g.qsize);
                if (g.cq_head == 0)
                    g.cq_phase ^= 1;
                *doorbell_cq(0) = g.cq_head; // CQ head doorbell

                uint16_t st = (uint16_t)(status >> 1);
                if (st == 0)
                {
                    con.println("NVMe: Identify Controller OK");
                    // ここで buf[24..63] などから MN/SN/FR を拾って表示するのも可
                    return true;
                }
                else
                {
                    con.printf("NVMe: Identify failed, status=%04x\n", (unsigned)st);
                    return false;
                }
            }
        }
        con.println("NVMe: Identify timeout");
        return false;
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

        // 完了待ち（簡易ポーリング）
        for (uint32_t spin = 0; spin < 10000000; ++spin)
        {
            const volatile CqEntry *ce = &g.acq[g.cq_head];
            uint16_t status = ce->status;
            if ((status & 1) == g.cq_phase)
            {
                // CQ 進める
                g.cq_head = (uint16_t)((g.cq_head + 1) % g.qsize);
                if (g.cq_head == 0)
                    g.cq_phase ^= 1;
                *doorbell_cq(0) = g.cq_head; // CQ head doorbell

                uint16_t sc = (uint16_t)(status >> 1);
                if (sc != 0)
                {
                    con.printf("NVMe: Identify NS #%u failed, status=%04x\n",
                               (unsigned)nsid, (unsigned)sc);
                    return false;
                }

                // 解析
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

                return true;
            }
        }
        con.println("NVMe: Identify NS timeout");
        return false;
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
        cc &= ~((0xFu << 20) | (0xFu << 16) | (0xFu << 4) | (0x3u << 5) | (0x7u << 7));
        cc |= (6u << 20); // IOSQES = 6 → 64B
        cc |= (4u << 16); // IOCQES = 4 → 16B
        cc |= (0u << 4);  // MPS=0
        cc |= (0u << 7);  // CSS=0
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

    uint64_t cap() { return g.cap_cache; }
    uint32_t vs() { return g.vs_cache; }

} // namespace nvme
