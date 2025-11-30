#include <stdint.h>

#include "driver/nvme/nvme_driver.hpp"
#include "driver/nvme/nvme_reg.hpp"
#include "driver/usb/keyboard/keyboard.hpp"
#include "fs/fat32/fat32_driver.hpp"
#include "fs/fat32/fat32.hpp"
#include "fs/installer.hpp"
#include "memory/memory_manager.hpp"
#include "memory/memory.hpp"
#include "pci/pci.hpp"
#include "shell/shell.hpp"
#include "sys/syscall.hpp"
#include "apic.hpp"
#include "boot_info.hpp"
#include "console.hpp"
#include "cxx.hpp"
#include "graphics.hpp"
#include "interrupt.hpp"
#include "io.hpp"
#include "ioapic.hpp"
#include "keyboard_layout.hpp"
#include "paging.hpp"
#include "pic.hpp"
#include "printk.hpp"
#include "segmentation.hpp"

// 色の定義 (0x00RRGGBB)
const uint32_t kColorWhite = 0xFFFFFFFF;
const uint32_t kColorBlack = 0xFF000000;
const uint32_t kColorGreen = 0xFF00FF00;
// Sylphia-OSっぽい背景色 (例: 少し青みがかったダークグレー)
const uint32_t kColorDesktopBG = 0xFF454545;

// ユーザーモード用のコードバイナリ
const uint8_t kUserCode[] = {
    // mov rdi, 0 (Root Directory Cluster)
    0x48, 0xc7, 0xc7, 0x00, 0x00, 0x00, 0x00,
    // mov rax, 3 (Syscall No.3 = ListDirectory)
    0x48, 0xc7, 0xc0, 0x03, 0x00, 0x00, 0x00,
    // syscall
    0x0f, 0x05,

    // mov rax, 2 (Syscall No.2 = Exit)
    0x48, 0xc7, 0xc0, 0x02, 0x00, 0x00, 0x00,
    // syscall
    0x0f, 0x05};

// 引数でエントリポイント(実行開始アドレス)を受け取るように変更
void JumpToUserMode(uint64_t entry_point)
{
    kprintf("[Kernel] Switching to Ring 3 (Entry: %lx)...\n", entry_point);

    // 1. ユーザー用のスタック確保
    const size_t kUserStackSize = 4096;
    void *user_stack = MemoryManager::Allocate(kUserStackSize);
    if (!user_stack)
    {
        kprintf("Failed to allocate user stack\n");
        return;
    }
    uint64_t user_rsp = reinterpret_cast<uint64_t>(user_stack) + kUserStackSize;

    // 2. コード領域の確保とコピー処理は削除 (呼び出し元で完了している前提)

    // 3. セグメント設定などはそのまま
    uint64_t rip = entry_point;
    uint16_t ss = kUserDS;   // 0x23
    uint16_t cs = kUserCS;   // 0x2B
    uint64_t rflags = 0x202; // IF=1

    // Ring 3 へ遷移
    __asm__ volatile(
        "mov %0, %%ds \n"
        "mov %0, %%es \n"
        "mov %0, %%fs \n"
        "mov %0, %%gs \n"
        "pushq %0 \n" // SS
        "pushq %1 \n" // RSP
        "pushq %2 \n" // RFLAGS
        "pushq %3 \n" // CS
        "pushq %4 \n" // RIP
        "iretq"
        :
        : "r"((uint64_t)ss), "r"(user_rsp), "r"(rflags), "r"((uint64_t)cs), "r"(rip));
}

extern "C" void EnableSSE();

extern "C" __attribute__((ms_abi)) void KernelMain(
    const FrameBufferConfig &config,
    const MemoryMap &memmap,
    const BootVolumeConfig &boot_volume)
{
    __asm__ volatile("cli");
    const uint32_t kDesktopBG = 0xFF181818;
    FillRectangle(config, 0, 0, config.HorizontalResolution, config.VerticalResolution, kDesktopBG);

    static Console console(config, 0xFFFFFFFF, kDesktopBG);
    g_console = &console;

    kprintf("Sylphia-OS Kernel v0.5.4\n");
    kprintf("----------------------\n");

    SetupSegments();
    SetupInterrupts();
    DisablePIC();
    EnableSSE();

    MemoryManager::Initialize(memmap);
    // kprintf("Memory Manager Initialized.\n");

    const size_t kKernelStackSize = 1024 * 16; // 16KB
    void *kernel_stack = MemoryManager::Allocate(kKernelStackSize);
    uint64_t kernel_stack_end = reinterpret_cast<uint64_t>(kernel_stack) + kKernelStackSize;
    SetKernelStack(kernel_stack_end);

    // kprintf("Kernel Stack setup complete at %lx\n", kernel_stack_end);

    PageManager::Initialize();
    InitializeSyscall();

    kprintf("Searching for NVMe Controller...\n");

    // PCIバスを探索してNVMeを探す (簡易実装)
    PCI::Device *nvme_dev = nullptr;
    PCI::Device found_dev; // コピー用

    for (int bus = 0; bus < 256; ++bus)
    {
        for (int dev = 0; dev < 32; ++dev)
        {
            for (int func = 0; func < 8; ++func)
            {
                PCI::Device d = {static_cast<uint8_t>(bus), static_cast<uint8_t>(dev), static_cast<uint8_t>(func)};
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
                    goto nvme_found; // 見つかったらループを抜ける
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

        uint8_t *check_buf = static_cast<uint8_t *>(MemoryManager::Allocate(512, 4096));
        NVMe::g_nvme->Read(2048, check_buf, 1);
        FileSystem::FAT32_BPB *check_bpb = reinterpret_cast<FileSystem::FAT32_BPB *>(check_buf);

        // シグネチャが 0xAA55 でなければ未フォーマットとみなす
        if (check_bpb->signature != 0xAA55)
        {
            kprintf("[Installer] Disk is empty. Starting formatting...\n");

            // ディスク容量を取得するゲッターがNVMeドライバに必要ですが、
            // 今は仮に固定値、または Identify で表示されていた値を使用します。
            // ※本来は NVMe::g_nvme->GetTotalBlocks() などを実装すべきです
            uint64_t disk_size = 1048576; // 例: 512MB分のセクタ数 (環境に合わせて調整してください)

            // 1. GPTパーティション作成
            FileSystem::FormatDiskGPT(disk_size);

            // 2. FAT32フォーマット (パーティション1)
            // LBA 2048 から最後まで
            FileSystem::FormatPartitionFAT32(disk_size - 2048);

            kprintf("[Installer] Format complete. Reboot is recommended but continuing...\n");
        }
        else
        {
            kprintf("[Installer] Valid file system detected.\n");
        }

        FileSystem::g_fat32_driver = new FileSystem::FAT32Driver(2048);
        FileSystem::g_fat32_driver->Initialize();

        kprintf("[Installer] Copying system files to NVMe...\n");

        if (boot_volume.bootloader_file.buffer)
        {
            // フォルダ作成: Root -> EFI
            uint32_t efi_cluster = FileSystem::g_fat32_driver->CreateDirectory("EFI        ");
            if (efi_cluster != 0)
            {
                // フォルダ作成: EFI -> BOOT
                uint32_t boot_cluster = FileSystem::g_fat32_driver->CreateDirectory("BOOT       ", efi_cluster);
                if (boot_cluster != 0)
                {
                    // ファイル書き込み: \EFI\BOOT\BOOTX64.EFI
                    FileSystem::g_fat32_driver->WriteFile(
                        "BOOTX64 EFI",
                        boot_volume.bootloader_file.buffer,
                        (uint32_t)boot_volume.bootloader_file.size,
                        boot_cluster);
                    kprintf("[Installer] Bootloader installed to \\EFI\\BOOT\\BOOTX64.EFI\n");

                    // カーネルも同じ場所に置くのが一般的
                    if (boot_volume.kernel_file.buffer)
                    {
                        FileSystem::g_fat32_driver->WriteFile(
                            "KERNEL  ELF",
                            boot_volume.kernel_file.buffer,
                            (uint32_t)boot_volume.kernel_file.size,
                            0);
                        kprintf("[Installer] Kernel installed to KERNEL.ELF\n");
                    }
                }
            }
        }

        const char *startup_script = "\\EFI\\BOOT\\BOOTX64.EFI";
        FileSystem::g_fat32_driver->WriteFile(
            "STARTUP NSH", // 8.3形式
            startup_script,
            21, // 文字列の長さ
            0);

        kprintf("[Installer] startup.nsh created.\n");

        kprintf("[Installer] Installation Complete!\n");
    }
    else
    {
        kprintf("NVMe Controller not found.\n");
    }

    PCI::SetupPCI();

    static Shell shell;
    g_shell = &shell;

    static LocalAPIC lapic;
    g_lapic = &lapic;
    g_lapic->Enable();

    IOAPIC::Enable(1, 0x40, g_lapic->GetID());
    // kprintf("I/O APIC: Keyboard (IRQ1) -> Vector 0x40\n");

    g_shell->OnKey(0);
    kprintf("\nWelcome to Sylphia-OS!\n");
    kprintf("Sylphia> ");

    __asm__ volatile("sti");

    while (1)
    {
        g_usb_keyboard->Update();
    }
    while (1)
        __asm__ volatile("hlt");
}