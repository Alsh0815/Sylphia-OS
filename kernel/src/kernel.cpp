#include <stdint.h>
#include "../../uefi/include/efi/base.h"
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
#include "kernel_runtime.hpp"
#include "painter.hpp"
#include "paging.hpp"
#include "pmm.hpp"
#include "console.hpp"

struct EFIMemoryDescriptor
{
    uint32_t Type;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
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
    if (cr3 == 0)
    {
        con.setColors({255, 255, 255}, {255, 0, 0});
        con.println("!!! PAGING INIT FAILED !!! --- SYSTEM HALTED ---");
        for (;;)
            __asm__ __volatile__("hlt");
    }
    con.printf("Paging: CR3=0x%p, mapped up to %u MiB\n",
               (void *)cr3, (unsigned)((paging::mapped_limit() >> 20)));

    // ▼▼▼ ここからが修正箇所 ▼▼▼

    // ページングが有効になったので、すぐにPMMを初期化する
    uint64_t managed = pmm::init(*bi);

    // PMMを使って新しいスタックを確保する (64KiB)
    const size_t stack_pages = 16;
    void *new_stack_base = pmm::alloc_pages(stack_pages);
    if (new_stack_base)
    {
        // スタックは上（アドレスの大きい方）から使うので、確保領域の終端を渡す
        uintptr_t sp = (uintptr_t)new_stack_base + (stack_pages * 4096);
        sp &= ~0xFULL; // 16B アライン

        // RDI に bi（SysV ABIの第1引数）を積んで、新スタックで kernel_after_stack へ
        asm volatile(
            "mov %0, %%rsp\n\t"
            "xor %%rbp, %%rbp\n\t"
            "call kernel_after_stack\n\t"
            :
            : "r"(sp), "D"(bi)
            : "memory");
        __builtin_unreachable();
    }

    // ▲▲▲ ここまで修正 ▲▲▲

    con.println("!!! FAILED TO ALLOCATE NEW STACK !!! --- SYSTEM HALTED ---");
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
    con.printf("Version: v.%d.%d.%d.%d\n", 0, 1, 4, 4);

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

    if (bi->kernel_ranges_ptr && bi->kernel_ranges_cnt)
    {
        auto *kr = (const PhysRange *)(uintptr_t)bi->kernel_ranges_ptr;
        for (uint32_t i = 0; i < bi->kernel_ranges_cnt; ++i)
        {
            pmm::reserve_range(kr[i].base, kr[i].pages);
        }
    }

    con.printf("PMM: managing up to %u MiB\n", (unsigned)(pmm::total_bytes() >> 20));
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
                // 2. probeしてみて、もし失敗したらmkfsを実行する
                Sylph1FS::MkfsOptions opt{};
                opt.version = 1;
                opt.minor_version = 0;
                opt.dir_bucket_count = 256;
                // 最初のパーティションを対象にする
                BlockDeviceSlice slice(*dev, parts[0].first_lba4k, parts[0].blocks4k);
                Sylph1FS fs(slice, con);
                // probeしてみて、もし失敗したらmkfsを実行する
                Sylph1FsDriver temp_driver;
                if (!temp_driver.probe(slice, con) || true)
                {
                    con.println("Sylph1FS: probe failed, attempting to format...");
                    if (fs.mkfs(opt) == FsStatus::Ok)
                    {
                        con.println("Sylph1FS: mkfs successful.");
                    }
                    else
                    {
                        con.println("Sylph1FS: mkfs failed.");
                    }
                };

                FsMount *mnt = nullptr;
                if (vfs::mount_auto(slice, con, &mnt) == FsStatus::Ok && mnt != nullptr)
                {
                    con.println("VFS: mount successful, trying readdir_root...");
                    auto sm = static_cast<Sylph1Mount *>(mnt);
                    sm->mkdir_path("/D", con);
                    SylphStat st;

                    if (sm->stat_path("/D", st, con))
                    {
                        con.printf("STAT /D: type=%u mode=%u links=%u size=%u ino=%u\n",
                                   (unsigned)st.type, (unsigned)st.mode, (unsigned)st.links,
                                   (unsigned long long)st.size, (unsigned long long)st.inode_id);
                    }

                    sm->create_path("/D/f", con);
                    sm->write_path("/D/f", "HELLO", 5, 0, con); // 非整列RMW対応済みならOK
                    if (sm->stat_path("/D/f", st, con))
                    {
                        con.printf("STAT /D/f: type=%u mode=%u links=%u size=%u ino=%u\n",
                                   (unsigned)st.type, (unsigned)st.mode, (unsigned)st.links,
                                   (unsigned long long)st.size, (unsigned long long)st.inode_id);
                    }
                    con.println("\n--- Huge File (Extent Expansion) Test ---");
                    sm->mkdir_path("/huge", con);
                    const char *huge_file_path = "/huge/file";

                    if (!sm->create_path(huge_file_path, con))
                    {
                        con.println("ERROR: Failed to create huge file.");
                    }
                    else
                    {
                        const int num_extents_to_force = 8;    // 4つ以上のエクステントを強制的に作る
                        const uint64_t chunk_size = 4096;      // 1ブロックずつ書き込む
                        const uint64_t seek_gap = 1024 * 1024; // 1MiBずつ間を空けて断片化させる

                        con.printf("Creating fragmented file with %d extents...\n", num_extents_to_force);

                        // 1. 断片化したデータを書き込み、間接エクステントブロックを使わせる
                        uint8_t *write_buf = (uint8_t *)pmm::alloc_pages(1);
                        ScopeExit free_write_buf([&]()
                                                 { pmm::free_pages(write_buf, 1); });

                        bool write_ok = true;
                        for (int i = 0; i < num_extents_to_force; ++i)
                        {
                            uint64_t offset = (uint64_t)i * seek_gap;
                            *(uint64_t *)write_buf = i; // 書き込むデータとしてチャンク番号を記録

                            if (!sm->write_path(huge_file_path, write_buf, chunk_size, offset, con))
                            {
                                con.printf("ERROR: Failed to write chunk %d!\n", i);
                                write_ok = false;
                                break;
                            }
                        }

                        if (write_ok)
                        {
                            con.println("Fragmented write successful. Verifying content...");

                            // 2. 書き込んだデータを読み戻し、内容が正しいか検証する
                            uint8_t *read_buf = (uint8_t *)pmm::alloc_pages(1);
                            ScopeExit free_read_buf([&]()
                                                    { pmm::free_pages(read_buf, 1); });
                            bool all_ok = true;
                            for (int i = 0; i < num_extents_to_force; ++i)
                            {
                                uint64_t offset = (uint64_t)i * seek_gap;
                                if (!sm->read_path(huge_file_path, read_buf, chunk_size, offset, con))
                                {
                                    con.printf("ERROR: Failed to read back chunk %d!\n", i);
                                    all_ok = false;
                                    break;
                                }
                                uint64_t pattern = *(uint64_t *)read_buf;
                                if (pattern != i)
                                {
                                    con.printf("ERROR: Data corruption in chunk %d! Expected=%d, Got=%llu\n", i, i, pattern);
                                    all_ok = false;
                                    break;
                                }
                            }

                            if (all_ok)
                            {
                                con.println("Data integrity verification successful.");
                            }
                        }

                        // 3. テストファイルをクリーンアップする
                        con.println("Cleaning up huge file...");
                        if (sm->unlink_path(huge_file_path, con))
                        {
                            con.println("Unlink successful.");
                        }
                        else
                        {
                            con.println("ERROR: Unlink failed!");
                        }
                    }
                    con.println("--- Huge File Test Complete ---");

                    con.println("\n--- Rename/Move Test ---");
                    sm->mkdir_path("/rename_test", con);
                    sm->mkdir_path("/rename_dest", con);
                    sm->create_path("/rename_test/file.txt", con);
                    sm->mkdir_path("/rename_test/subdir", con);

                    con.println("\nInitial state:");
                    sm->readdir_path("/rename_test", con);

                    // 1. ファイルの名前変更 (同じディレクトリ内)
                    con.println("\n1. Renaming file.txt to file_renamed.txt...");
                    sm->rename_path("/rename_test/file.txt", "/rename_test/file_renamed.txt", con);

                    // 2. ディレクトリの名前変更 (同じディレクトリ内)
                    con.println("\n2. Renaming subdir to subdir_renamed...");
                    sm->rename_path("/rename_test/subdir", "/rename_test/subdir_renamed", con);

                    con.println("\nState after renames:");
                    sm->readdir_path("/rename_test", con);

                    // 3. ファイルを別ディレクトリへ移動
                    con.println("\n3. Moving file_renamed.txt to /rename_dest...");
                    sm->rename_path("/rename_test/file_renamed.txt", "/rename_dest/file_moved.txt", con);

                    // 4. ディレクトリを別ディレクトリへ移動
                    con.println("\n4. Moving subdir_renamed to /rename_dest...");
                    sm->rename_path("/rename_test/subdir_renamed", "/rename_dest/subdir_moved", con);

                    con.println("\nFinal state:");
                    con.println("--- Source Dir [/rename_test]:");
                    sm->readdir_path("/rename_test", con); // 空になっているはず
                    con.println("--- Destination Dir [/rename_dest]:");
                    sm->readdir_path("/rename_dest", con); // 2つのエントリがあるはず

                    con.println("--- Rename/Move Test Complete ---");
                }
                else
                {
                    con.printf("VFS: mount failed after probe/mkfs.\n");
                }
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
