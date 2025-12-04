#include <stdint.h>

#include "apic.hpp"
#include "boot_info.hpp"
#include "console.hpp"
#include "cxx.hpp"
#include "driver/nvme/nvme_driver.hpp"
#include "driver/nvme/nvme_reg.hpp"
#include "driver/usb/keyboard/keyboard.hpp"
#include "driver/usb/mass_storage/mass_storage.hpp"
#include "fs/fat32/fat32.hpp"
#include "fs/fat32/fat32_driver.hpp"
#include "fs/installer.hpp"
#include "graphics.hpp"
#include "interrupt.hpp"
#include "io.hpp"
#include "ioapic.hpp"
#include "keyboard_layout.hpp"
#include "memory/memory.hpp"
#include "memory/memory_manager.hpp"
#include "paging.hpp"
#include "pci/pci.hpp"
#include "pic.hpp"
#include "printk.hpp"
#include "segmentation.hpp"
#include "shell/shell.hpp"
#include "sys/std/file_descriptor.hpp"
#include "sys/syscall.hpp"

FileDescriptor *g_fds[16];

// 色の定義 (0x00RRGGBB)
const uint32_t kColorWhite = 0xFFFFFFFF;
const uint32_t kColorBlack = 0xFF000000;
const uint32_t kColorGreen = 0xFF00FF00;
// Sylphia-OSっぽい背景色 (例: 少し青みがかったダークグレー)
const uint32_t kColorDesktopBG = 0xFF454545;

bool CopyFile(FileSystem::FAT32Driver *src_fs, const char *src_path,
              FileSystem::FAT32Driver *dst_fs, const char *dst_path)
{
    uint32_t size = src_fs->GetFileSize(src_path);
    if (size == 0)
    {
        kprintf("File not found or empty: %s\n", src_path);
        return false;
    }

    kprintf("Copying file from %s to %s... (%d bytes)\n", src_path, dst_path,
            size);

    uint8_t *buf = static_cast<uint8_t *>(MemoryManager::Allocate(size));
    if (!buf)
    {
        kprintf("Failed to allocate buffer for file copy.\n");
        return false;
    }

    if (src_fs->ReadFile(src_path, buf, size) != size)
    {
        kprintf("Read failed.\n");
        MemoryManager::Free(buf, size);
        return false;
    }

    char dir_part[64];
    const char *filename_part = dst_path;
    const char *last_slash = nullptr;

    for (const char *p = dst_path; *p; ++p)
    {
        if (*p == '/')
            last_slash = p;
    }

    uint32_t parent_cluster = 0;

    if (last_slash)
    {
        int len = last_slash - dst_path;
        if (len > 63)
            len = 63;
        for (int i = 0; i < len; ++i)
            dir_part[i] = dst_path[i];
        dir_part[len] = '\0';

        filename_part = last_slash + 1;

        if (len > 0)
        {
            parent_cluster = dst_fs->EnsureDirectory(dir_part);
            if (parent_cluster == 0)
            {
                kprintf("Failed to ensure directory: %s\n", dir_part);
                MemoryManager::Free(buf, size);
                return false;
            }
        }
    }

    char name83[11];
    FileSystem::FAT32Driver::To83Format(filename_part, name83);

    dst_fs->WriteFile(name83, buf, size, parent_cluster);

    MemoryManager::Free(buf, size);
    return true;
}

extern "C" void EnableSSE();

extern "C" __attribute__((ms_abi)) void
KernelMain(const FrameBufferConfig &config, const MemoryMap &memmap)
{
    __asm__ volatile("cli");
    const uint32_t kDesktopBG = 0xFF181818;
    FillRectangle(config, 0, 0, config.HorizontalResolution,
                  config.VerticalResolution, kDesktopBG);

    static Console console(config, 0xFFFFFFFF, kDesktopBG);
    g_console = &console;

    SetupSegments();
    SetupInterrupts();
    DisablePIC();
    EnableSSE();

    MemoryManager::Initialize(memmap);

    const size_t kKernelStackSize = 1024 * 16; // 16KB
    void *kernel_stack = MemoryManager::Allocate(kKernelStackSize);
    uint64_t kernel_stack_end =
        reinterpret_cast<uint64_t>(kernel_stack) + kKernelStackSize;
    SetKernelStack(kernel_stack_end);

    kprintf("Kernel Stack setup complete at %lx\n", kernel_stack_end);

    PageManager::Initialize();
    InitializeSyscall();

    // Standard I/O Initialization
    g_fds[0] = new KeyboardFD(); // Stdin
    g_fds[1] = new ConsoleFD();  // Stdout
    g_fds[2] = new ConsoleFD();  // Stderr
    kprintf("Standard I/O Initialized (FD 0, 1, 2).\n");

    kprintf("Searching for NVMe Controller...\n");

    // PCIバスを探索してNVMeを探す (簡易実装)
    PCI::Device *nvme_dev = nullptr;
    PCI::Device found_dev; // コピー用

    PCI::SetupPCI();

    __asm__ volatile("sti");

    for (int bus = 0; bus < 256; ++bus)
    {
        for (int dev = 0; dev < 32; ++dev)
        {
            for (int func = 0; func < 8; ++func)
            {
                PCI::Device d = {static_cast<uint8_t>(bus),
                                 static_cast<uint8_t>(dev),
                                 static_cast<uint8_t>(func)};
                uint16_t vendor = PCI::ReadConfReg(d, 0x00) & 0xFFFF;

                if (vendor == 0xFFFF)
                    continue;

                // クラスコード取得
                uint32_t reg8 = PCI::ReadConfReg(d, 0x08);
                uint8_t base = (reg8 >> 24) & 0xFF;
                uint8_t sub = (reg8 >> 16) & 0xFF;

                // Base=0x01 (Mass Storage), Sub=0x08 (Non-Volatile)
                if (base == 0x01 && sub == 0x08)
                {
                    kprintf("Found NVMe at %d:%d.%d\n", bus, dev, func);
                    found_dev = d;
                    nvme_dev = &found_dev;
                    goto nvme_found;
                }
            }
        }
    }

nvme_found:
    if (nvme_dev)
    {
        uintptr_t bar0 = PCI::ReadBar0(*nvme_dev);
        // kprintf("NVMe BAR0 Address: %lx\n", bar0);
        NVMe::g_nvme = new NVMe::Driver(bar0);
        NVMe::g_nvme->Initialize();
        NVMe::g_nvme->IdentifyController();
        NVMe::g_nvme->CreateIOQueues();

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

        FileSystem::FAT32Driver *nvme_fs =
            new FileSystem::FAT32Driver(NVMe::g_nvme, 2048);
        nvme_fs->Initialize();

        FileSystem::g_system_fs = nvme_fs;
        FileSystem::g_fat32_driver = nvme_fs;

        if (USB::g_mass_storage)
        {
            kprintf("USB Mass Storage Detected. Checking for updates...\n");

            uint8_t *buf = static_cast<uint8_t *>(MemoryManager::Allocate(512));
            USB::g_mass_storage->Read(0, buf, 1);

            uint64_t usb_part_lba = 0;

            if (buf[510] == 0x55 && buf[511] == 0xAA)
            {
                bool is_bpb = (buf[0] == 0xEB || buf[0] == 0xE9);

                if (!is_bpb)
                {
                    uint32_t start_lba =
                        *reinterpret_cast<uint32_t *>(&buf[0x1BE + 8]);
                    kprintf("MBR detected. Partition 1 starts at LBA %d\n",
                            start_lba);
                    usb_part_lba = start_lba;
                }
                else
                {
                    kprintf("BPB detected at LBA 0 (Superfloppy format).\n");
                }
            }
            MemoryManager::Free(buf, 512);

            FileSystem::FAT32Driver *usb_fs =
                new FileSystem::FAT32Driver(USB::g_mass_storage, usb_part_lba);
            usb_fs->Initialize();

            if (!already_installed)
            {
                kprintf("[Installer] Performing initial file copy...\n");
                nvme_fs->EnsureDirectory("sys");
                nvme_fs->EnsureDirectory("sys/bin");
                nvme_fs->EnsureDirectory("home");

                CopyFile(usb_fs, "EFI/BOOT/BOOTX64.EFI", nvme_fs,
                         "EFI/BOOT/BOOTX64.EFI");
                CopyFile(usb_fs, "apps/stdio.elf", nvme_fs,
                         "sys/bin/stdio.elf");
                CopyFile(usb_fs, "apps/test.elf", nvme_fs, "sys/bin/test.elf");
                CopyFile(usb_fs, "kernel.elf", nvme_fs, "kernel.elf");

                kprintf("Update process finished.\n");

                const char *startup_script = "\\EFI\\BOOT\\BOOTX64.EFI";
                nvme_fs->WriteFile("STARTUP NSH", startup_script, 21, 0);
                kprintf("[Installer] startup.nsh created.\n");

                kprintf("[Installer] Installation Complete!\n");
            }
        }
    }
    else
    {
        kprintf("NVMe Controller not found.\n");
    }

    static Shell shell;
    g_shell = &shell;

    static LocalAPIC lapic;
    g_lapic = &lapic;
    g_lapic->Enable();
    IOAPIC::Enable(1, 0x40, g_lapic->GetID());
    g_lapic->StartTimer(10, 0x20);

    g_shell->OnKey(0);
    kprintf("\nWelcome to Sylphia-OS!\n");
    kprintf("Sylphia> ");

    while (1)
        __asm__ volatile("hlt");
}