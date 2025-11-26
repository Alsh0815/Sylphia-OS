#include <stdint.h>

#include "driver/nvme/nvme_driver.hpp"
#include "driver/nvme/nvme_reg.hpp"
#include "fs/fat32/fat32_driver.hpp"
#include "fs/fat32/fat32.hpp"
#include "fs/installer.hpp"
#include "memory/memory_manager.hpp"
#include "memory/memory.hpp"
#include "pci/pci.hpp"
#include "shell/shell.hpp"
#include "apic.hpp"
#include "console.hpp"
#include "graphics.hpp"
#include "interrupt.hpp"
#include "io.hpp"
#include "ioapic.hpp"
#include "keyboard.hpp"
#include "pic.hpp"
#include "printk.hpp"
#include "segmentation.hpp"

// 色の定義 (0x00RRGGBB)
const uint32_t kColorWhite = 0xFFFFFFFF;
const uint32_t kColorBlack = 0xFF000000;
const uint32_t kColorGreen = 0xFF00FF00;
// Sylphia-OSっぽい背景色 (例: 少し青みがかったダークグレー)
const uint32_t kColorDesktopBG = 0xFF454545;

// Shiftキーの状態管理フラグ
bool g_shift_pressed = false;

// 使用するキーボード配列設定 (ここで切り替え可能！)
// KeyboardLayout kCurrentLayout = KeyboardLayout::US_Standard;
const KeyboardLayout kCurrentLayout = KeyboardLayout::JP_Standard; // 日本語配列の場合

// 割り込みフレーム構造体 (CPUがスタックに積む情報)
struct InterruptFrame
{
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

// ■ キーボード割り込みハンドラ
__attribute__((interrupt)) void KeyboardHandler(InterruptFrame *frame)
{
    uint8_t scancode = IoIn8(0x60);
    bool is_break = (scancode & 0x80) != 0;
    uint8_t keycode = scancode & 0x7F;

    if (keycode == 0x2A || keycode == 0x36)
    {
        g_shift_pressed = !is_break;
    }
    else if (!is_break)
    {
        char ascii = ConvertScanCodeToAscii(keycode, g_shift_pressed, kCurrentLayout);
        if (ascii != 0 && g_shell)
        {
            g_shell->OnKey(ascii);
        }
    }

    g_lapic->EndOfInterrupt();
}

extern "C" __attribute__((ms_abi)) void KernelMain(const FrameBufferConfig &config, const MemoryMap &memmap)
{
    __asm__ volatile("cli");
    const uint32_t kDesktopBG = 0xFF181818;
    FillRectangle(config, 0, 0, config.HorizontalResolution, config.VerticalResolution, kDesktopBG);

    static Console console(config, 0xFFFFFFFF, kDesktopBG);
    g_console = &console;

    kprintf("Sylphia-OS Kernel v0.5.3\n");
    kprintf("----------------------\n");

    SetupSegments();
    SetupInterrupts();
    DisablePIC();

    MemoryManager::Initialize(memmap);
    kprintf("Memory Manager Initialized.\n");

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
        kprintf("NVMe BAR0 Address: %lx\n", bar0);
        NVMe::g_nvme = new NVMe::Driver(bar0);
        NVMe::g_nvme->Initialize();
        NVMe::g_nvme->IdentifyController();
        NVMe::g_nvme->CreateIOQueues();

        uint64_t total_blocks = 2097152; // 仮の値 (Identify Namespaceで取得したnszeを使うべき)
        FileSystem::FormatDiskGPT(total_blocks);
        uint64_t part_size = total_blocks - 2048 - 34;
        FileSystem::FormatPartitionFAT32(part_size);

        FileSystem::FAT32Driver fat_driver(2048);
        fat_driver.Initialize();
        // 書き込むデータ
        const char *file_content = "Hello from Sylphia-OS! This file is on FAT32.";
        // ファイル名 (8.3形式: 11文字固定)
        // "TEST    TXT" -> TEST.TXT
        fat_driver.WriteFile("TEST    TXT", file_content, 45);
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

    SetIDTEntry(0x40, (uint64_t)KeyboardHandler, 0x08, 0xE);

    IOAPIC::Enable(1, 0x40, g_lapic->GetID());
    kprintf("I/O APIC: Keyboard (IRQ1) -> Vector 0x40\n");

    g_shell->OnKey(0); // OnKey経由ではなく直接PrintPromptを呼びたいが、privateなので
                       // 本来は publicに PrintPromptを用意して呼ぶのが綺麗です。
                       // 今回は OnKeyの実装を見ると、'\n'以外ではプロンプトが出ないので、
                       // kprintf("Sylphia> "); をここで呼んでおきます。
    kprintf("\nWelcome to Sylphia-OS!\n");
    kprintf("Sylphia> ");

    __asm__ volatile("sti");

    while (1)
        __asm__ volatile("hlt");
}