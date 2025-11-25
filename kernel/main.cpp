#include <stdint.h>

#include "memory/memory_manager.hpp"
#include "memory/memory.hpp"
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
    const uint32_t kDesktopBG = 0xFF454545;
    FillRectangle(config, 0, 0, config.HorizontalResolution, config.VerticalResolution, kDesktopBG);

    static Console console(config, 0xFFFFFFFF, kDesktopBG);
    g_console = &console;

    kprintf("Sylphia-OS Kernel v0.5\n");
    kprintf("----------------------\n");

    SetupSegments();
    SetupInterrupts();
    DisablePIC();

    // ■ メモリマップの確認 ■
    kprintf("Checking Memory Map...\n");
    kprintf("Map Base: %x, Size: %d bytes, DescSize: %d\n",
            memmap.buffer, memmap.map_size, memmap.descriptor_size);
    uintptr_t iter = reinterpret_cast<uintptr_t>(memmap.buffer);
    for (unsigned int i = 0; i < memmap.map_size / memmap.descriptor_size; ++i)
    {
        auto *desc = reinterpret_cast<const MemoryDescriptor *>(iter);
        if (static_cast<MemoryType>(desc->type) == MemoryType::kEfiConventionalMemory)
        {
            if (desc->number_of_pages > 256)
            {
                kprintf("FREE: Addr=%x, Pages=%d\n", desc->physical_start, desc->number_of_pages);
            }
        }

        iter += memmap.descriptor_size;
    }

    MemoryManager::Initialize(memmap);
    kprintf("Memory Manager Initialized.\n");

    // new のテスト
    int *p = new int;
    *p = 123;
    kprintf("Dynamic Allocation Test: int* p = 0x%x, *p = %d\n", p, *p);

    // 配列 new のテスト
    char *str = new char[20];
    str[0] = 'H';
    str[1] = 'e';
    str[2] = 'a';
    str[3] = 'p';
    str[4] = '!';
    str[5] = '\0';
    kprintf("String Allocation Test: %s\n", str);

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