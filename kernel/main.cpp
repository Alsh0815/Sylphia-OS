#include <stdint.h>

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

extern "C" __attribute__((ms_abi)) void KernelMain(const FrameBufferConfig &config)
{
    __asm__ volatile("cli");
    const uint32_t kDesktopBG = 0xFF454545;
    FillRectangle(config, 0, 0, config.HorizontalResolution, config.VerticalResolution, kDesktopBG);

    // コンソール初期化 & グローバル変数へセット
    // ※static変数にすることでスタック領域ではなくデータ領域に配置し、
    // KernelMainのループ中も生存期間を保証する（今回は無限ループ内なのでローカルでも動くが、作法として）
    static Console console(config, 0xFFFFFFFF, kDesktopBG);
    g_console = &console;

    kprintf("Sylphia-OS Kernel v0.4.2\n");
    kprintf("----------------------\n");

    SetupSegments();
    SetupInterrupts();
    DisablePIC();

    static Shell shell;
    g_shell = &shell;

    // Local APICの準備
    static LocalAPIC lapic;
    g_lapic = &lapic;
    g_lapic->Enable();

    // CPU IDを表示して、APICレジスタが読めているか確認
    kprintf("Local APIC: Enabled (Core ID: %d)\n", g_lapic->GetID());

    // 1. キーボードハンドラをIDTに登録
    // ベクタ番号は 0x40 (64) にします
    // カーネルコードセグメントは 0x08
    // ゲートタイプ 0xE (Interrupt Gate)
    SetIDTEntry(0x40, (uint64_t)KeyboardHandler, 0x08, 0xE);

    // I/O APICの設定
    // キーボード(IRQ 1) を ベクタ 0x40 に割り当て、Core 0 (g_lapic->GetID()) に送る
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