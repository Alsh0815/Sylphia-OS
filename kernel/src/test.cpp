#include "driver/pci/nvme/nvme.hpp"
#include "console.hpp"
#include "pmm.hpp"
#include "paging.hpp"

static void fill_pattern(uint8_t *p, size_t n, uint8_t seed)
{
    for (size_t i = 0; i < n; ++i)
        p[i] = (uint8_t)(seed + (uint8_t)i * 7u);
}
static void hexdump16(Console &con, const uint8_t *p)
{
    for (int i = 0; i < 16; i++)
        con.printf("%x ", (unsigned)p[i]);
}

static bool cmp_and_log(Console &con, const uint8_t *a, const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (a[i] != b[i])
        {
            con.printf("Mismatch at +%u  exp=%x got=%x\n", (unsigned)i, (unsigned)a[i], (unsigned)b[i]);
            size_t s = (i >= 8 ? i - 8 : 0);
            size_t e = ((i + 8) < n ? i + 8 : n - 1);
            con.printf(" exp: ");
            hexdump16(con, a + s);
            con.printf("\n got: ");
            hexdump16(con, b + s);
            con.printf("\n");
            return false;
        }
    return true;
}

// NVMe WRITE/READ round-trip selftest.
// It preserves on-disk data by backing up and restoring after verification.
bool nvme_selftest_write(Console &con, uint32_t nsid, uint64_t base_slba)
{
    const size_t lba_size = nvme::lba_bytes();
    if (lba_size == 0)
    {
        con.println("NVMe selftest: invalid LBA size from driver");
        return false;
    }

    bool all_ok = true;

    auto one_case = [&](const char *name, uint64_t slba, void *buf_va, size_t bytes) -> bool
    {
        // Allocate helper buffers (backup + verify)
        void *backup = pmm::alloc_pages((bytes + 4095) / 4096);
        void *verify = pmm::alloc_pages((bytes + 4095) / 4096);
        if (!backup || !verify)
        {
            con.printf("[%s] alloc failed\n", name);
            return false;
        }

        // 1) Backup current media content
        if (!nvme::read_lba(nsid, slba, (uint16_t)(bytes / lba_size), backup, bytes, con))
        {
            con.printf("[%s] pre-read (backup) failed\n", name);
            return false;
        }

        // 2) Prepare test pattern in buf_va (already positioned to trigger PRP path)
        fill_pattern((uint8_t *)buf_va, bytes, 0x5Au);

        // 3) WRITE pattern
        if (!nvme::write_lba(nsid, slba, (uint16_t)(bytes / lba_size), buf_va, bytes, nvme::kWriteNone, con))
        {
            con.printf("[%s] WRITE failed\n", name);
            return false;
        }
        if (!nvme::flush(nsid, con))
        {
            con.printf("[%s] FLUSH failed\n", name);
            return false;
        }

        // 4) READ back into verify
        if (!nvme::read_lba(nsid, slba, (uint16_t)(bytes / lba_size), verify, bytes, con))
        {
            con.printf("[%s] READ-back failed\n", name);
            return false;
        }

        // 5) Compare
        bool ok = cmp_and_log(con, (const uint8_t *)buf_va, (const uint8_t *)verify, bytes);
        con.printf("[%s] verify %s\n", name, ok ? "OK" : "NG");

        // 6) Restore original media content
        if (!nvme::write_lba(nsid, slba, (uint16_t)(bytes / lba_size), backup, bytes, nvme::kWriteFua, con))
        {
            con.printf("[%s] RESTORE failed (data left modified!)\n", name);
            all_ok = false; // still continue to report failure
        }
        /*
        if (!nvme::flush(nsid, con))
        {
            con.printf("[%s] FLUSH (RESTORE) failed\n", name);
            return false;
        }
        */

        return ok;
    };

    // --- Case A: PRP1 only (fits in one page) ---
    {
        void *buf = pmm::alloc_pages(1); // 4KiB aligned
        size_t bytes = 4096;             // 8 LBAs
        if (!buf)
        {
            con.println("[PRP1] alloc failed");
            return false;
        }
        uint64_t slba = base_slba; // e.g., 4096 or any safe scratch region
        bool ok = one_case("PRP1", slba, buf, bytes);
        all_ok = all_ok && ok;
    }

    // --- Case B: PRP2 (spans exactly into the 2nd page) ---
    {
        void *base = pmm::alloc_pages(2); // contiguous 8KiB
        if (!base)
        {
            con.println("[PRP2] alloc failed");
            return false;
        }
        uint8_t *buf = (uint8_t *)base + (4096 - 256); // offset so first page has 256B room
        size_t bytes = 1024;                           // 2 LBAs -> 256 + 768 (2 pages total)
        uint64_t slba = base_slba + 8;                 // separate region
        bool ok = one_case("PRP2", slba, buf, bytes);
        all_ok = all_ok && ok;
    }

    // --- Case C: PRP List (>2 pages, e.g., 10KiB = 20 LBAs) ---
    {
        const size_t bytes = 10 * 1024; // 20 LBAs
        void *base = pmm::alloc_pages((bytes + 4095) / 4096 + 1);
        if (!base)
        {
            con.println("[PRP List] alloc failed");
            return false;
        }
        uint8_t *buf = (uint8_t *)base; // page-aligned start
        uint64_t slba = base_slba + 16; // separate region
        bool ok = one_case("PRP-List", slba, buf, bytes);
        all_ok = all_ok && ok;
    }

    con.printf("NVMe WRITE selftest: %s\n", all_ok ? "ALL PASSED" : "FAILED");
    return all_ok;
}

bool nvme_test_flush_quirk(Console &con, uint32_t nsid, uint64_t base_slba)
{
    con.println("\n--- Starting Minimal Flush Quirk Test ---");
    const size_t lba_size = nvme::lba_bytes();
    if (lba_size == 0)
        return false;

    const size_t bytes = lba_size; // 1ブロックだけテスト
    void *buf = pmm::alloc_pages(1);
    if (!buf)
    {
        con.println("[QuirkTest] alloc failed");
        return false;
    }

    // --- Case 1: 非ゼロデータ書き込み → FLUSH (失敗が予測される) ---
    con.println("\n[QuirkTest] Case 1: Writing non-zero pattern...");
    fill_pattern((uint8_t *)buf, bytes, 0x5A);

    if (!nvme::write_lba(nsid, base_slba, 1, buf, bytes, nvme::kWriteNone, con))
    {
        con.println("[QuirkTest] Case 1: WRITE failed unexpectedly.");
        return false; // WRITE自体が失敗した場合はテスト中断
    }
    con.println("[QuirkTest] Case 1: WRITE command completed.");

    // このFLUSHがタイムアウトで失敗すると仮説は裏付けられる
    if (!nvme::flush(nsid, con))
    {
        con.println("[QuirkTest] Case 1: FLUSH failed as expected. This strongly suggests a QEMU quirk.");
        // 本来はここでコントローラリセットが必要だが、今回はテストを続行せず終了する
        con.println("--- Minimal Flush Quirk Test Finished ---");
        return false; // テストとしては「失敗」だが、原因究明としては「成功」
    }

    con.println("[QuirkTest] Case 1: FLUSH succeeded unexpectedly. Hypothesis may be wrong.");
    con.println("--- Minimal Flush Quirk Test Finished ---");
    return true;
}

bool nvme_test_read_then_flush(Console &con, uint32_t nsid, uint64_t base_slba)
{
    con.println("\n--- Starting Read-then-Flush Test ---");
    const size_t lba_size = nvme::lba_bytes();
    if (lba_size == 0)
        return false;

    const size_t bytes = lba_size;
    void *buf = pmm::alloc_pages(1);
    if (!buf)
    {
        con.println("[ReadFlushTest] alloc failed");
        return false;
    }

    // 1. READを実行
    con.println("[ReadFlushTest] Issuing READ command...");
    if (!nvme::read_lba(nsid, base_slba, 1, buf, bytes, con))
    {
        con.println("[ReadFlushTest] READ failed unexpectedly.");
        return false;
    }
    con.println("[ReadFlushTest] READ command completed.");

    // 2. 直後にFLUSHを実行
    // このFLUSHが失敗すれば、read_lbaに原因があることが確定する
    con.println("[ReadFlushTest] Issuing FLUSH command...");
    if (!nvme::flush(nsid, con))
    {
        con.println("[ReadFlushTest] FLUSH failed after READ. The culprit is likely in read_lba()!");
        return false;
    }

    con.println("[ReadFlushTest] FLUSH succeeded after READ. The problem is more complex.");
    con.println("--- Read-then-Flush Test Finished ---");
    return true;
}