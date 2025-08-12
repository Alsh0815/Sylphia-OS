#include "nvme.hpp"
#include "nvme_regs.hpp"
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
        uint32_t dw0;
        uint32_t dw1;
        uint16_t sq_head;
        uint16_t sq_id;
        uint16_t cid;
        uint16_t status; // bit0: Phase
    } __attribute__((packed));

    struct Ctx
    {
        NvmeRegs *r;
        uint32_t db_stride; // 4 << DSTRD
        SqEntry *admin_sq;
        CqEntry *admin_cq;
        uint16_t asq_n; // エントリ数（実数、例: 32）
        uint16_t acq_n; // エントリ数
        uint16_t sq_tail;
        uint16_t cq_head;
        uint8_t cq_phase;
        uint64_t cap_cache;
        uint32_t vs_cache;
    } g{};

    static inline void bzero(void *p, uint64_t n)
    {
        uint8_t *q = (uint8_t *)p;
        for (uint64_t i = 0; i < n; i++)
            q[i] = 0;
    }

    static bool wait_rdy(int want, uint32_t loops /*適当なポーリング回数*/)
    {
        for (uint32_t i = 0; i < loops; ++i)
        {
            if (((g.r->CSTS) & 1u) == (uint32_t)want)
                return true;
        }
        return false;
    }

    static uint32_t cap_mqes(uint64_t cap) { return (uint32_t)(cap & 0xFFFF); }
    static uint32_t cap_dstrd(uint64_t cap) { return (uint32_t)((cap >> 32) & 0xF); }
    static uint32_t cap_to_100ms(uint64_t cap) { return (uint32_t)((cap >> 24) & 0xFF); } // 単位100ms

    bool init(uint64_t bar0_phys, Console &con)
    {
        g.r = (NvmeRegs *)(uintptr_t)bar0_phys;

        // キャッシュ
        g.cap_cache = g.r->CAP;
        g.vs_cache = g.r->VS;

        // まず停止（EN=0）→ RDY=0 待ち
        g.r->CC &= ~1u;
        {
            // CAP.TO は100ms単位。余裕を持って待つ
            uint32_t to = cap_to_100ms(g.cap_cache);
            if (to == 0)
                to = 10; // 既定
            if (!wait_rdy(0, to * 100000))
            {
                con.println("NVMe: disable timeout");
                return false;
            }
        }

        // ドアベルストライド
        g.db_stride = 4u << cap_dstrd(g.cap_cache);

        // Admin Queue メモリ確保（各1ページ=4KiB）
        // キューサイズは CAP.MQES(最大-1) に収める
        uint16_t max_entries = (uint16_t)(cap_mqes(g.cap_cache) + 1); // CAPは「最大-1」
        uint16_t want = 32;
        if (want > max_entries)
            want = max_entries;
        if (want < 2)
            want = 2; // 最低2

        g.asq_n = want;
        g.acq_n = want;

        g.admin_sq = (SqEntry *)pmm::alloc_pages(1);
        g.admin_cq = (CqEntry *)pmm::alloc_pages(1);
        if (!g.admin_sq || !g.admin_cq)
        {
            con.println("NVMe: alloc admin queues failed");
            return false;
        }
        bzero(g.admin_sq, 4096);
        bzero(g.admin_cq, 4096);

        g.sq_tail = 0;
        g.cq_head = 0;
        g.cq_phase = 1;

        // AQA: 下位16bit=ASQS(0-based)、上位16bit=ACQS(0-based)
        uint32_t aqa = ((uint32_t)(g.acq_n - 1) << 16) | (uint32_t)(g.asq_n - 1);
        g.r->AQA = aqa;

        // ASQ/ACQ: 物理アドレス
        g.r->ASQ = (uint64_t)(uintptr_t)g.admin_sq;
        g.r->ACQ = (uint64_t)(uintptr_t)g.admin_cq;

        // CC 設定：MPS=0(4KiB), CSS=0(NVM), AMS=0, EN=1
        uint32_t cc = g.r->CC;
        // MPS(7:4), AMS(6:5), CSS(9:7) を 0 に
        cc &= ~((0xFu << 4) | (0x3u << 5) | (0x7u << 7));
        cc |= (0u << 4); // MPS=0 (4KiB)
        cc |= (0u << 7); // CSS=0 (NVM)
        cc |= 1u;        // EN=1
        g.r->CC = cc;

        {
            uint32_t to = cap_to_100ms(g.cap_cache);
            if (to == 0)
                to = 10;
            if (!wait_rdy(1, to * 100000))
            {
                con.println("NVMe: enable timeout");
                return false;
            }
        }

        con.printf("NVMe: admin queues ready. VS=%08x MQES=%u DSTRD=%u\n",
                   g.vs_cache, (unsigned)cap_mqes(g.cap_cache), (unsigned)cap_dstrd(g.cap_cache));
        return true;
    }

    uint64_t cap() { return g.cap_cache; }
    uint32_t vs() { return g.vs_cache; }

} // namespace nvme
