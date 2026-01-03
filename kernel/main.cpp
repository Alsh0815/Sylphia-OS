#include "arch/inasm.hpp"
#include <stdint.h>

#include "apic.hpp"
#include "console.hpp"
#include "cxx.hpp"
#include "driver/nvme/nvme_driver.hpp"
#include "fs/fat32/fat32.hpp"
#include "fs/fat32/fat32_driver.hpp"
#include "fs/installer.hpp"
#include "graphic/GraphicSystem.hpp"
#include "ioapic.hpp"
#include "memory/memory_manager.hpp"
#include "pci/pci.hpp"
#include "printk.hpp"
#include "sys/init/init.hpp"
#include "sys/logger/logger.hpp"
#include "sys/std/file_descriptor.hpp"
#include "sys/timer/tsc.hpp"
#include "task/idle_task.hpp"
#include "task/scheduler.hpp"
#include "task/task_manager.hpp"

FileDescriptor *g_fds[16];

extern "C" __attribute__((ms_abi)) void
KernelMain(const FrameBufferConfig &config, const MemoryMap &memmap)
{
    CLI();

#if defined(__aarch64__)
    // UART直書きデバッグ (QEMU virt: 0x09000000)
    volatile char *uart = reinterpret_cast<volatile char *>(0x09000000);
    *uart = '[';
    *uart = '1';
    *uart = ']';
    *uart = ' '; // [1] Entry
#endif

    // 1. グラフィックシステム初期化（メモリ初期化不要）
    Graphic::InitializeGraphics(config);

    // 2. 画面クリア（新APIで直接フレームバッファに描画）
    const uint32_t kDesktopBG = 0xFF181818;
    Graphic::FillScreen(kDesktopBG);

#if defined(__aarch64__)
    *uart = '[';
    *uart = '2';
    *uart = ']';
    *uart = ' '; // [2] After FillRect
#endif

    // 3. カーネルコア初期化（メモリマネージャ含む）
    Sys::Init::InitializeCore(memmap);

    // 4. コンソール初期化
    static Console console(*Graphic::g_llr, 0xFFFFFFFF, kDesktopBG);
    g_console = &console;

#if defined(__aarch64__)
    *uart = '[';
    *uart = '3';
    *uart = ']';
    *uart = ' '; // [3] Console ready
#endif

    // 5. 標準I/Oとロガー初期化
    Sys::Init::InitializeIO();

    // 4. PCIデバイス初期化（xHCI + NVMe）
#if defined(__aarch64__)
    PCI::InitializePCI(config.EcamBaseAddress, config.EcamStartBus,
                       config.EcamEndBus);
#endif
    PCI::SetupPCI();
    kprintf("[Kernel] DEBUG: PCI Setup returned.\n");

    STI();

    // 5. ファイルシステム初期化とインストーラー
    if (NVMe::g_nvme)
    {
        kprintf(
            "[Kernel] DEBUG: NVMe Driver exists. Allocating check buffer...\n");
        uint8_t *check_buf =
            static_cast<uint8_t *>(MemoryManager::Allocate(512, 4096));

        kprintf("[Kernel] DEBUG: Check buffer allocated at %lx. Reading LBA "
                "2048...\n",
                (uint64_t)check_buf);
#if defined(__aarch64__)
        {
            uint64_t sp_val;
            __asm__ volatile("mov %0, sp" : "=r"(sp_val));
            kprintf("[Kernel] DEBUG: SP = %lx, 16-byte aligned = %s\n", sp_val,
                    (sp_val & 0xf) == 0 ? "YES" : "NO");

            // スタック書き込みテスト
            volatile uint64_t test_val = 0xDEADBEEF;
            kprintf("[Kernel] DEBUG: Stack write test = %lx\n", test_val);

            // 明示的なストア命令テスト (stp相当)
            uint64_t dummy1 = 0x1234, dummy2 = 0x5678;
            __asm__ volatile("stp %0, %1, [sp, #-16]!\n\t"
                             "ldp %0, %1, [sp], #16"
                             : "+r"(dummy1), "+r"(dummy2)
                             :
                             : "memory");
            kprintf(
                "[Kernel] DEBUG: STP/LDP test OK (dummy1=%lx, dummy2=%lx)\n",
                dummy1, dummy2);

            // vtable確認
            uint64_t *obj_ptr = reinterpret_cast<uint64_t *>(NVMe::g_nvme);
            uint64_t vtable_ptr = *obj_ptr; // 先頭がvtableポインタ
            uint64_t *vtable = reinterpret_cast<uint64_t *>(vtable_ptr);
            kprintf("[Kernel] DEBUG: g_nvme object at %lx\n",
                    (uint64_t)obj_ptr);
            kprintf("[Kernel] DEBUG: vtable at %lx\n", vtable_ptr);
            kprintf("[Kernel] DEBUG: vtable[0] (Read?) = %lx\n", vtable[0]);
            kprintf("[Kernel] DEBUG: vtable[1] (Write?) = %lx\n", vtable[1]);

            // 仮想関数呼び出しを回避して直接呼び出し
            kprintf("[Kernel] DEBUG: Calling ReadLBA directly...\n");
            NVMe::g_nvme->ReadLBA(2048, check_buf, 1);
            kprintf("[Kernel] DEBUG: ReadLBA complete.\n");

            // 仮想関数呼び出しテスト
            kprintf("[Kernel] DEBUG: Now trying virtual Read()...\n");
        }
#endif
        NVMe::g_nvme->Read(2048, check_buf, 1);
        kprintf("[Kernel] DEBUG: Read LBA 2048 complete.\n");

        FileSystem::FAT32_BPB *check_bpb =
            reinterpret_cast<FileSystem::FAT32_BPB *>(check_buf);

        bool already_installed = false;

        // シグネチャが 0xAA55 でなければ未フォーマットとみなす
        if (check_bpb->signature != 0xAA55)
        {
            kprintf("[Installer] Disk is empty. Starting formatting...\n");

            uint64_t disk_size = 1048576;

            FileSystem::FormatDiskGPT(disk_size);
            FileSystem::FormatPartitionFAT32(disk_size - 2048);

            kprintf("[Installer] Format complete. Reboot is recommended but "
                    "continuing...\n");
        }
        else
        {
            already_installed = true;
            kprintf("[Installer] Valid file system detected.\n");
        }

        MemoryManager::Free(check_buf, 512);

        FileSystem::FAT32Driver *nvme_fs =
            new FileSystem::FAT32Driver(NVMe::g_nvme, 2048);
        nvme_fs->Initialize();

        FileSystem::g_system_fs = nvme_fs;
        FileSystem::g_fat32_driver = nvme_fs;

        // USBからのインストール処理
        kprintf("[Kernel] DEBUG: Starting Installer...\n");
        FileSystem::RunInstaller(nvme_fs, already_installed);
    }
    else
    {
        kprintf("NVMe Controller not found.\n");
    }

    // 6. APIC設定 (x86_64のみ)
#if defined(__x86_64__)
    static LocalAPIC lapic;
    g_lapic = &lapic;
    g_lapic->Enable();
    Sys::Logger::g_event_logger->Info(Sys::Logger::LogType::Kernel,
                                      "Local APIC enabled.");
    IOAPIC::Enable(1, 0x40, g_lapic->GetID());
#endif

    // 7. タスクマネージャとスケジューラの初期化
    TaskManager::Initialize();
    Scheduler::Initialize();
    InitializeIdleTask();
    kprintf("[Kernel] Multitasking initialized.\n");

    kprintf("\nWelcome to Sylphia-OS!\n");
    kprintf("[Kernel] Starting scheduler... Shell will be auto-started.\n");

    // スケジューラを有効化
    Scheduler::Enable();

    // タイマー開始（スケジューラ有効化後）(x86_64のみ)
    // スケジューラが有効になるとIdleTaskに切り替わり、
    // IdleTaskが必須プロセス（シェル等）を自動起動する
#if defined(__x86_64__)
    g_lapic->StartTimer(10, 0x20);
#endif

    // 8. メインループ（ここには到達しないはず）
    // IdleTaskがスケジュールされ、以降の実行はそちらで行われる
    while (1)
        Hlt();
}