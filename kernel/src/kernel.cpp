#include <stdint.h>
#include "../include/bootinfo.h"
#include "../include/framebuffer.hpp"
#include "../include/font8x8.hpp"
#include "driver/pci/nvme/nvme.hpp"
#include "driver/pci/nvme/nvme_regs.hpp"
#include "driver/pci/pci.hpp"
#include "io/block/block_device.hpp"
#include "io/block/block_registry.hpp"
#include "io/block/block_slice.hpp"
#include "io/fs/sylph-v1/sylph1fs_driver.hpp"
#include "io/fs/vfs.hpp"
#include "io/partitions/gpt.hpp"
#include "gdt.hpp"
#include "heap.hpp"
#include "idt.hpp"
#include "painter.hpp"
#include "paging.hpp"
#include "pmm.hpp"
#include "console.hpp"

struct EFIMemoryDescriptor
{
    uint32_t Type;
    uint32_t Pad;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
};

enum
{
    EfiBootServicesCode = 3,
    EfiBootServicesData = 4,
    EfiConventionalMemory = 7 /* boot_services.h の列挙と合わせる */
};

static inline void bzero(void *p, size_t n)
{
    uint8_t *q = (uint8_t *)p;
    for (size_t i = 0; i < n; i++)
        q[i] = 0;
}

extern bool nvme_selftest_write(Console &con, uint32_t nsid, uint64_t base_slba);
extern bool nvme_test_flush_quirk(Console &con, uint32_t nsid, uint64_t base_slba);
extern bool nvme_test_read_then_flush(Console &con, uint32_t nsid, uint64_t base_slba);

extern "C" __attribute__((sysv_abi)) void kernel_main(BootInfo *bi)
{
    if (!bi || !bi->fb_base || bi->width == 0 || bi->height == 0)
        for (;;)
            __asm__ __volatile__("hlt");

    Framebuffer fb(*bi);
    fb.clear({10, 12, 24});

    Painter paint(fb);
    Console con(fb, paint);

    // タイトル
    fb.fillRect(0, 0, fb.width(), 24, {32, 120, 255});
    paint.setColor({255, 255, 255});
    uint32_t tx = 8, ty = 6;
    uint32_t right = fb.width() - 8;
    paint.setTextLayout(8, 12);
    paint.drawTextWrap(tx, ty, "SYLPHIA OS (text-color-clip)", right);

    con.setColors({255, 255, 255}, {0, 0, 0});
    con.println("Framebuffer Info:");
    con.print_kv("W", bi->width);
    con.print_kv("H", bi->height);
    con.print_kv("Pitch", bi->pitch);

    con.print_bg(
        "Highlighted long line with background will wrap seamlessly across the clip area.",
        /*fg*/ {0, 0, 0},
        /*bg*/ {255, 220, 40});

    ((volatile uint32_t *)(uintptr_t)bi->fb_base)[0] = 0x00FFFF;
    uint64_t cr3 = paging::init_identity(*bi);
    con.printf("Paging: CR3=0x%p, mapped up to %u MiB\n",
               (void *)cr3, (unsigned)((paging::mapped_limit() >> 20)));

    // ここで低位スタックを確保して移行（もう新CR3なので確実にマップ済み）
    paging::init_allocator(*bi);
    void *new_sp = paging::alloc_low_stack(16 * 4096); // 64KiB くらい
    if (new_sp)
    {
        uintptr_t sp = ((uintptr_t)new_sp) & ~0xFULL; // 16B アライン
        // RDI に bi（SysV ABIの第1引数）を積んで、新スタックで kernel_after_stack へ
        asm volatile(
            "mov %0, %%rsp\n\t"
            "xor %%rbp, %%rbp\n\t"        // フレームポインタ無効化（お守り）
            "call kernel_after_stack\n\t" // ← 戻らない設計
            :
            : "r"(sp), "D"(bi)
            : "memory");
        __builtin_unreachable(); // ここには来ない想定
    }

    for (;;)
        __asm__ __volatile__("hlt");
}

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val)
{
    uint32_t lo = (uint32_t)val, hi = (uint32_t)(val >> 32);
    asm volatile("wrmsr" ::"c"(msr), "a"(lo), "d"(hi));
}

static inline void enable_nxe()
{
    uint64_t efer = rdmsr(0xC0000080); // IA32_EFER
    efer |= (1ull << 11);              // NXE=1
    wrmsr(0xC0000080, efer);
}

// kernel.cpp
extern "C" __attribute__((sysv_abi)) void kernel_after_stack(BootInfo *bi)
{
    enable_nxe();

    // ここは「新しいスタック」上。ローカルを作り直してOK
    Framebuffer fb(*bi);
    Painter paint(fb);
    Console con(fb, paint);

    con.clear_fullscreen();

    fb.fillRect(0, 0, fb.width(), 24, {32, 120, 255});
    paint.setColor({255, 255, 255});
    uint32_t tx = 8, ty = 6;
    uint32_t right = fb.width() - 8;
    paint.setTextLayout(8, 12);
    paint.drawTextWrap(tx, ty, "SYLPHIA OS (text-color-clip)", right);

    con.setColors({255, 255, 255}, {0, 0, 0});
    con.printf("Version: v.%d.%d.%d.%d\n", 0, 1, 4, 3);

    con.println("Switched to low stack.");

    void *ist_pages = pmm::alloc_pages(8); // 8 * 4KiB = 32KiB
    uint64_t ist_top = (uint64_t)ist_pages + 8 * 4096;

    // 2) GDT/TSS を初期化（IST1に上をセット）
    if (!gdt::init(ist_top))
    {
        con.println("GDT/TSS init failed.");
    }
    else
    {
        con.println("GDT/TSS loaded (IST1 ready).");
    }

    idt::init(bi);
    idt::install_double_fault(1);

    uint64_t managed = pmm::init(*bi);

    if (bi->kernel_ranges_ptr && bi->kernel_ranges_cnt)
    {
        auto *kr = (const PhysRange *)(uintptr_t)bi->kernel_ranges_ptr;
        for (uint32_t i = 0; i < bi->kernel_ranges_cnt; ++i)
        {
            pmm::reserve_range(kr[i].base, kr[i].pages);
        }
    }

    con.printf("PMM: managing up to %u MiB\n", (unsigned)(managed >> 20));
    con.printf("PMM: total=%u MiB free=%u MiB used=%u MiB\n",
               (unsigned)(pmm::total_bytes() >> 20),
               (unsigned)(pmm::free_bytes() >> 20),
               (unsigned)(pmm::used_bytes() >> 20));

    if (heap::init(256 * 1024))
    {
        con.printf("Heap2: cap=%u KiB remain=%u KiB\n",
                   (unsigned)(heap::capacity() >> 10), (unsigned)(heap::remain() >> 10));
        void *a = heap::kmalloc(2000, 16, true);
        void *b = heap::kmalloc(5000, 16, false);
        heap::kfree(a);
        void *c = heap::kmalloc(1500, 16, false); // a の穴を再利用できるはず
        b = heap::krealloc(b, 9000);              // 後方拡張 or 移動
        con.printf("used=%u KiB remain=%u KiB\n",
                   (unsigned)(heap::used() >> 10), (unsigned)(heap::remain() >> 10));
    }
    else
    {
        con.println("Heap init failed.");
    }

    pci::Device nvme{};
    if (pci::scan_nvme(con, nvme))
    {
        const uint64_t BAR0 = nvme.bar[0];
        con.printf("NVMe BAR0 (phys) = %p\n", (void *)BAR0);

        const uint64_t TEST_VA = 0x0000000200000000ull; // 8 GiB
        if (!paging::map_mmio_at(TEST_VA, BAR0, 0x200000))
        {
            con.println("map_mmio_at failed");
        }

        volatile NvmeRegs *r = (volatile NvmeRegs *)(uintptr_t)TEST_VA;
        uint64_t cap = r->CAP;
        uint32_t vs = r->VS;
        con.printf("NVMe CAP@lowVA=%x VS=%x\n",
                   (unsigned long long)cap, vs);

        /*
        if (!nvme::init((void *)(uintptr_t)TEST_VA, con))
        {
            con.println("NVMe init failed.");
        }
        if (!nvme::create_io_queues(con, 64))
        {
            con.println("Create NVMe I/O queues failed.");
        }
        */

        if (!nvme::init_and_create_queues((void *)(uintptr_t)TEST_VA, con, 64))
        {
            con.println("NVMe init and create queues failed.");
        }

        block::NvmeInitParams p{.bar0_va = (void *)(uintptr_t)TEST_VA, .nsid = 1};
        BlockDevice *dev = block::open_nvme_as_block(p, con);
        if (!dev)
        {
            con.printf("Error: open_nvme_as_block");
        }

        PartitionInfo parts[32];
        size_t found = 0;
        gpt::GptMeta meta{};

        if (gpt::scan(*dev, parts, 32, &found, &meta, con))
        {
            register_sylph1fs_driver();

            if (found > 0)
            {
                // 最初のパーティションを対象にする
                BlockDeviceSlice slice(*dev, parts[0].first_lba4k, parts[0].blocks4k);

                // --- ここから追加 ---

                // probeしてみて、もし失敗したらmkfsを実行する
                Sylph1FsDriver temp_driver;
                if (!temp_driver.probe(slice, con))
                {
                    con.println("Sylph1FS: probe failed, attempting to format...");
                    Sylph1FS fs(slice, con);
                    Sylph1FS::MkfsOptions opt{}; // デフォルトオプションでフォーマット
                    if (fs.mkfs(opt) == FsStatus::Ok)
                    {
                        con.println("Sylph1FS: mkfs successful.");
                    }
                    else
                    {
                        con.println("Sylph1FS: mkfs failed.");
                    }
                }

                // --- ここまで追加 ---
            }

            FsMount *mnt = nullptr;
            FsStatus st = vfs::mount_auto_on_partitions(*dev, parts, found, con, &mnt);
            if (st != FsStatus::Ok)
            {
                con.printf("mount_auto_on_partitions failed (%d)\n", (int)st);
            }
        }
    }
    else
    {
        con.println("NVMe not present.");
    }

    con.println("Fin.");
    for (;;)
        __asm__ __volatile__("hlt");
}
