#include <stdint.h>

#include "apic.hpp"
#include "console.hpp"
#include "driver/nvme/nvme_driver.hpp"
#include "fs/fat32/fat32.hpp"
#include "fs/fat32/fat32_driver.hpp"
#include "fs/installer.hpp"
#include "graphics.hpp"
#include "ioapic.hpp"
#include "memory/memory_manager.hpp"
#include "pci/pci.hpp"
#include "printk.hpp"
#include "shell/shell.hpp"
#include "sys/init/init.hpp"
#include "sys/logger/logger.hpp"
#include "sys/std/file_descriptor.hpp"

FileDescriptor *g_fds[16];

extern "C" __attribute__((ms_abi)) void
KernelMain(const FrameBufferConfig &config, const MemoryMap &memmap)
{
    __asm__ volatile("cli");

    // 1. コンソール初期化
    const uint32_t kDesktopBG = 0xFF181818;
    FillRectangle(config, 0, 0, config.HorizontalResolution,
                  config.VerticalResolution, kDesktopBG);

    static Console console(config, 0xFFFFFFFF, kDesktopBG);
    g_console = &console;

    // 2. カーネルコア初期化
    Sys::Init::InitializeCore(memmap);

    // 3. 標準I/Oとロガー初期化
    Sys::Init::InitializeIO();

    // 4. PCIデバイス初期化（xHCI + NVMe）
    PCI::SetupPCI();

    __asm__ volatile("sti");

    // 5. ファイルシステム初期化とインストーラー
    if (NVMe::g_nvme)
    {
        uint8_t *check_buf =
            static_cast<uint8_t *>(MemoryManager::Allocate(512, 4096));
        NVMe::g_nvme->Read(2048, check_buf, 1);
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
        FileSystem::RunInstaller(nvme_fs, already_installed);
    }
    else
    {
        kprintf("NVMe Controller not found.\n");
    }

    // 6. シェル起動
    static Shell shell;
    g_shell = &shell;

    // 7. APIC設定
    static LocalAPIC lapic;
    g_lapic = &lapic;
    g_lapic->Enable();
    Sys::Logger::g_event_logger->Info(Sys::Logger::LogType::Kernel,
                                      "Local APIC enabled.");
    IOAPIC::Enable(1, 0x40, g_lapic->GetID());
    g_lapic->StartTimer(10, 0x20);

    // 8. シェル表示
    g_shell->OnKey(0);
    Sys::Logger::g_event_logger->Info(Sys::Logger::LogType::Kernel,
                                      "Shell initialized.");
    kprintf("\nWelcome to Sylphia-OS!\n");
    kprintf("Sylphia> ");

    // 9. メインループ
    while (1)
        __asm__ volatile("hlt");
}