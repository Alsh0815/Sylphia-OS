#include "interrupt.hpp"
#include "Debug.hpp"
#include "apic.hpp"
#include "arch/inasm.hpp"
#include "console.hpp"
#include "driver/usb/keyboard/keyboard.hpp"
#include "io.hpp"
#include "keyboard_layout.hpp"
#include "printk.hpp"
#include "sys/timer/timer.hpp"
#include "task/scheduler.hpp"
#include <stdint.h>

#if defined(__aarch64__)
#include "arch/aarch64/gic.hpp"
#include "arch/aarch64/timer_arch.hpp"
#endif

// AArch64割り込みハンドラ (C++呼び出し用)
#if defined(__aarch64__)
extern "C" void AArch64IrqHandler(void *frame)
{
    uint32_t iar = Arch::AArch64::GIC::AcknowledgeInterrupt();
    uint32_t irq = iar & 0x3FF;

    // Timer Interrupt (INTID 30)
    if (irq == 30)
    {
        // ティック更新
        Sys::Timer::Tick();

        // 次のタイマーイベントを設定（ここで再設定しないと次は来ない）
        Arch::AArch64::Timer::SetIntervalMs(10);
        Arch::AArch64::Timer::Enable(); // 必要なら有効化

        // キーボード更新
        if (g_usb_keyboard)
        {
            g_usb_keyboard->Update();
        }

        // スケジューラ
        Scheduler::Schedule();
    }
    else if (irq == 27)
    {
        // Virtual Timer (27) が有効になっている場合のストーム対策
        // 無効化して要因をクリアする
        asm volatile("msr cntv_ctl_el0, %0" ::"r"((uint64_t)0));
        asm volatile("msr cntv_tval_el0, %0" ::"r"((uint64_t)0));
        // kprintf("[IRQ] Virtual Timer (27) suppressed.\n");
    }
    else
    {
        kprintf("[IRQ] Unknown IRQ: %d\n", irq);
    }

    Arch::AArch64::GIC::EndOfInterrupt(iar);
}
#endif

#if defined(__x86_64__)

// IDTの実体 (256個の割り込みに対応)
InterruptDescriptor idt[256];

extern "C" uint64_t GetCR2();

const uint32_t kBsodBgColor = 0xFF0000AA; // 濃い青 (Windows BSOD風)
const uint32_t kWhiteColor = 0xFFFFFFFF;
const uint32_t kYellowColor = 0xFFFFFF00;
const uint32_t kRedColor = 0xFFFF0000;

// IDTR (lidt命令に渡す構造体)
struct IDTR
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// 指定した番号の割り込みにハンドラ関数を登録するヘルパー
// (今回はまだハンドラがないので使いませんが、枠組みとして作ります)
void SetIDTEntry(int index, uint64_t offset, uint16_t selector, uint16_t type)
{
    idt[index].offset_low = offset & 0xFFFF;
    idt[index].offset_middle = (offset >> 16) & 0xFFFF;
    idt[index].offset_high = (offset >> 32) & 0xFFFFFFFF;

    idt[index].segment_selector = selector;
    idt[index].ist = 0;
    idt[index].type = type;
    idt[index].dpl = 0;     // カーネル権限
    idt[index].present = 1; // 有効化
    idt[index].reserved_1 = 0;
    idt[index].reserved_2 = 0;
    idt[index].reserved_3 = 0;
}

// IDTをCPUにロードする
void LoadIDT(uint16_t limit, uint64_t base)
{
    IDTR idtr;
    idtr.limit = limit;
    idtr.base = base;
    __asm__ volatile("lidt %0" ::"m"(idtr));
}

__attribute__((interrupt)) void DivideErrorHandler(InterruptFrame *frame)
{
    if (g_console)
        g_console->SetColor(kRedColor, kBsodBgColor);

    kprintf("\n========================================\n");
    kprintf("      DIVIDE ERROR EXCEPTION (#DE)      \n");
    kprintf("========================================\n\n");

    if (g_console)
        g_console->SetColor(kWhiteColor, kBsodBgColor);
    kprintf("The kernel attempted to divide a number by zero.\n\n");
    kprintf("RIP: %lx  CS: %lx\n", frame->rip, frame->cs);
    kprintf("RFLAGS: %lx\n", frame->rflags);

    kprintf("\nSystem Halted.\n");
    while (1)
        Hlt();
}

__attribute__((interrupt)) void InvalidOpcodeHandler(InterruptFrame *frame)
{
    if (g_console)
        g_console->SetColor(kRedColor, kBsodBgColor);

    kprintf("\n========================================\n");
    kprintf("      INVALID OPCODE EXCEPTION (#UD)    \n");
    kprintf("========================================\n\n");

    if (g_console)
        g_console->SetColor(kWhiteColor, kBsodBgColor);
    kprintf("The processor encountered an invalid instruction.\n");
    kprintf("This usually means the Instruction Pointer (RIP) is corrupt\n");
    kprintf("or pointing to data instead of code.\n\n");

    kprintf("RIP: %lx  CS: %lx\n", frame->rip, frame->cs);
    kprintf("RFLAGS: %lx\n", frame->rflags);

    kprintf("\nSystem Halted.\n");
    while (1)
        Hlt();
}

__attribute__((interrupt)) void DoubleFaultHandler(InterruptFrame *frame,
                                                   uint64_t error_code)
{
    if (g_console)
        g_console->SetColor(kRedColor, kBsodBgColor);

    kprintf("\n========================================\n");
    kprintf("    CRITICAL DOUBLE FAULT (#DF)         \n");
    kprintf("========================================\n\n");

    if (g_console)
        g_console->SetColor(kWhiteColor, kBsodBgColor);
    kprintf("The processor failed to handle an exception.\n");
    kprintf("This is a critical system failure.\n\n");

    kprintf("RIP: %lx  CS: %lx\n", frame->rip, frame->cs);
    kprintf("RFLAGS: %lx\n", frame->rflags);
    kprintf("Error Code: %lx\n", error_code);

    kprintf("\nSystem Halted.\n");
    while (1)
        Hlt();
}

__attribute__((interrupt)) void GPFaultHandler(InterruptFrame *frame,
                                               uint64_t error_code)
{
    if (g_console)
        g_console->SetColor(kRedColor, kBsodBgColor);
    kprintf("\n========================================\n");
    kprintf("   GENERAL PROTECTION FAULT (#GP)   \n");
    kprintf("========================================\n\n");

    if (g_console)
        g_console->SetColor(kWhiteColor, kBsodBgColor);
    kprintf("A fatal exception has occurred at:\n");
    kprintf("RIP   : %lx\n", frame->rip);
    kprintf("CS    :  %lx\n", frame->cs);
    kprintf("RFLAGS: %lx\n", frame->rflags);
    kprintf("RSP   : %lx\n", frame->rsp);
    kprintf("SS    :  %lx\n", frame->ss);
    kprintf("Error Code: %lx\n", error_code);

    kprintf("System Halted. Please reset the machine.\n");

    while (1)
        Hlt();
}

__attribute__((interrupt)) void PageFaultHandler(InterruptFrame *frame,
                                                 uint64_t error_code)
{
    uint64_t cr2 = GetCR2();

    if (g_console)
        g_console->SetColor(kYellowColor, kBsodBgColor);
    kprintf("\n========================================\n");
    kprintf("         PAGE FAULT DETECTED!         \n");
    kprintf("========================================\n\n");

    if (g_console)
        g_console->SetColor(kWhiteColor, kBsodBgColor);
    kprintf("The kernel tried to access an invalid memory address.\n\n");

    kprintf("Accessed Address (CR2): %lx\n", cr2);
    kprintf("Instruction Pointer (RIP): %lx\n", frame->rip);
    kprintf("Error Code: %lx\n", error_code);

    kprintf("\nReason Analysis:\n");
    kprintf("  - ");
    if (!(error_code & 1))
        kprintf("Page Not Present (Invalid Address)");
    else
        kprintf("Protection Violation (Access Rights)");
    kprintf("\n  - ");

    if (error_code & 2)
        kprintf("Write Operation");
    else
        kprintf("Read Operation");
    kprintf("\n  - ");

    if (error_code & 4)
        kprintf("User Mode Cause");
    else
        kprintf("Kernel Mode Cause");

    kprintf("\n\n");
    kprintf("System Halted.\n");

    while (1)
        Hlt();
}

extern USB::XHCI::Controller *g_xhci;
extern USB::Keyboard *g_usb_keyboard;

__attribute__((interrupt)) void UsbInterruptHandler(InterruptFrame *frame)
{
    kprintf("[IRQ] USB Interrupt!\\n");
    // xHCIコントローラーのEvent Ringを処理
    if (g_xhci)
    {
        g_xhci->ProcessInterrupt();
    }
    // LAPIC EOI (End of Interrupt) を送信
    if (g_lapic)
    {
        g_lapic->EndOfInterrupt();
    }
}

__attribute__((interrupt)) void TimerHandler(InterruptFrame *frame)
{
    // ティックカウンタをインクリメント
    Sys::Timer::Tick();

    // EOIを先に送信（コンテキストスイッチ前に送信することが重要）
    if (g_lapic)
    {
        g_lapic->EndOfInterrupt();
    }

    // USB処理を実行
    extern USB::Keyboard *g_usb_keyboard;
    if (g_usb_keyboard)
    {
        g_usb_keyboard->Update();
    }

    Scheduler::Schedule();
}

#endif

void SetupInterrupts()
{
#if defined(__x86_64__)
    // ゼロ除算例外 (Vector 0)
    SetIDTEntry(0, (uint64_t)DivideErrorHandler, 0x08, IDT_TYPE_INTERRUPT_GATE);

    // 無効オペコード例外 (Vector 6)
    SetIDTEntry(6, (uint64_t)InvalidOpcodeHandler, 0x08,
                IDT_TYPE_INTERRUPT_GATE);

    // ダブルフォールト例外 (Vector 8)
    SetIDTEntry(8, (uint64_t)DoubleFaultHandler, 0x08, IDT_TYPE_INTERRUPT_GATE);

    // 一般保護例外 (Vector 13)
    SetIDTEntry(13, (uint64_t)GPFaultHandler, 0x08, IDT_TYPE_INTERRUPT_GATE);

    // ページフォールト (Vector 14)
    // 0x08 = カーネルコードセグメント (CS)
    // 0xE  = IDT_TYPE_INTERRUPT_GATE (割り込みゲート)
    SetIDTEntry(14, (uint64_t)PageFaultHandler, 0x08, IDT_TYPE_INTERRUPT_GATE);

    SetIDTEntry(0x20, (uint64_t)TimerHandler, 0x08, IDT_TYPE_INTERRUPT_GATE);

    // USB xHCI割り込み (Vector 0x50)
    SetIDTEntry(0x50, (uint64_t)UsbInterruptHandler, 0x08,
                IDT_TYPE_INTERRUPT_GATE);

    LoadIDT(sizeof(idt) - 1, (uint64_t)&idt[0]);
#endif
}