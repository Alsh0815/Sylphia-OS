#include "idt.hpp"
#include "../include/framebuffer.hpp"
#include "painter.hpp"
#include "console.hpp"

namespace
{
    static uint16_t g_cs_selector = 0;

    // ===== IDT エントリ/ポインタ =====
    struct __attribute__((packed)) IdtEntry
    {
        uint16_t offset_low;
        uint16_t selector;
        uint8_t ist;       // IST[2:0] | 0
        uint8_t type_attr; // 0x8E: present+interrupt gate
        uint16_t offset_mid;
        uint32_t offset_high;
        uint32_t zero;
    };

    struct __attribute__((packed)) IdtPtr
    {
        uint16_t limit;
        uint64_t base;
    };

    static IdtEntry g_idt[256];
    static IdtPtr g_idtr;

    // 例外ハンドラのフレーム
    struct __attribute__((packed)) InterruptFrame
    {
        uint64_t rip;
        uint16_t cs;
        uint16_t _pad1;
        uint32_t _pad2;
        uint64_t rflags;
        uint64_t rsp;
        uint16_t ss;
        uint16_t _pad3;
        uint32_t _pad4;
    };

    static const BootInfo *g_bi = nullptr; // 画面出力用に保持

    inline void set_gate(uint8_t vec, void (*handler)(), uint8_t ist = 0)
    {
        uint64_t addr = (uint64_t)handler;
        g_idt[vec].offset_low = (uint16_t)(addr & 0xFFFF);
        g_idt[vec].selector = g_cs_selector; // ★ 現在のCSを使う
        g_idt[vec].ist = ist & 0x7;
        g_idt[vec].type_attr = 0x8E; // present | DPL=0 | type=14 (IntGate)
        g_idt[vec].offset_mid = (uint16_t)((addr >> 16) & 0xFFFF);
        g_idt[vec].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
        g_idt[vec].zero = 0;
    }

    inline uint64_t read_cr2()
    {
        uint64_t v;
        asm volatile("mov %%cr2, %0" : "=r"(v));
        return v;
    }

    static volatile bool nmi_in_progress = false; // ★ 再入防止

    // 画面に例外情報を出す（Consoleをその場で作り直す）
    void print_panic_header(const char *name)
    {
        Framebuffer fb(*g_bi);
        Painter p(fb);
        Console c(fb, p);

        c.clear_fullscreen({0, 0, 0});
        fb.fillRect(0, 0, fb.width(), 24, {160, 40, 40});
        uint32_t x = 8, y = 6;
        p.setColor({255, 255, 255});
        p.setTextLayout(8, 12);
        p.drawTextWrap(x, y, "EXCEPTION", fb.width() - 8);
        c.setColors({255, 255, 255}, {0, 0, 0});
        c.println("");
        c.print_bg(name, {0, 0, 0}, {255, 220, 40});
        c.println("");
    }

    __attribute__((interrupt)) static void isr_nmi(InterruptFrame *frame)
    {
        // 再入防止（NMIはマスク不可で再入し得る）
        if (!nmi_in_progress)
        {
            nmi_in_progress = true;
            print_panic_header("NMI (Non-Maskable Interrupt)");
            Framebuffer fb(*g_bi);
            Painter p(fb);
            Console c(fb, p);
            c.printf("RIP=0x%p  RFLAGS=0x%llx\n",
                     (void *)frame->rip, (unsigned long long)frame->rflags);
            c.println("System entered NMI. Halting for diagnostics.");
        }
        for (;;)
            asm volatile("hlt");
    }

    // ===== ハンドラ（interrupt attribute） =====
    __attribute__((interrupt)) static void isr_de(InterruptFrame *frame)
    {
        print_panic_header("#DE Divide-by-zero");
        Framebuffer fb(*g_bi);
        Painter p(fb);
        Console c(fb, p);
        c.printf("RIP=0x%p  CS=0x%x  RFLAGS=0x%llx\n",
                 (void *)frame->rip, (unsigned)frame->cs, (unsigned long long)frame->rflags);
        for (;;)
            asm volatile("hlt");
    }

    __attribute__((interrupt)) static void isr_bp(InterruptFrame *frame)
    {
        print_panic_header("#BP Breakpoint");
        Framebuffer fb(*g_bi);
        Painter p(fb);
        Console c(fb, p);
        c.printf("RIP=0x%p\n", (void *)frame->rip);
        for (;;)
            asm volatile("hlt");
    }

    __attribute__((interrupt)) static void isr_ud(InterruptFrame *frame)
    {
        print_panic_header("#UD Invalid opcode");
        Framebuffer fb(*g_bi);
        Painter p(fb);
        Console c(fb, p);
        c.printf("RIP=0x%p  RFLAGS=0x%llx\n",
                 (void *)frame->rip, (unsigned long long)frame->rflags);
        for (;;)
            asm volatile("hlt");
    }

    __attribute__((interrupt)) static void isr_gp(InterruptFrame *frame, uint64_t error)
    {
        print_panic_header("#GP General Protection Fault");
        Framebuffer fb(*g_bi);
        Painter p(fb);
        Console c(fb, p);
        c.printf("RIP=0x%p  ERR=0x%llx  RFLAGS=0x%llx\n",
                 (void *)frame->rip, (unsigned long long)error, (unsigned long long)frame->rflags);
        for (;;)
            asm volatile("hlt");
    }

    __attribute__((interrupt)) static void isr_pf(InterruptFrame *frame, uint64_t error)
    {
        print_panic_header("#PF Page Fault");
        Framebuffer fb(*g_bi);
        Painter p(fb);
        Console c(fb, p);
        uint64_t cr2 = read_cr2();
        c.printf("RIP=0x%p  CR2=0x%p  ERR=0x%llx\n",
                 (void *)frame->rip, (void *)cr2, (unsigned long long)error);
        c.println("ERR bits: P=1(not-present) | W=2(write) | U=4(user) | RSVD=8 | I=16(inst)");
        for (;;)
            asm volatile("hlt");
    }

} // anon

namespace idt
{

    void init(const BootInfo *bi)
    {
        g_bi = bi;

        asm volatile ("mov %%cs, %0" : "=r"(g_cs_selector));

        // IDT を0クリア
        for (int i = 0; i < 256; i++)
        {
            g_idt[i] = IdtEntry{};
        }

        // 例外ハンドラを設定
        set_gate(VEC_NMI, (void (*)())isr_nmi);
        set_gate(VEC_DE, (void (*)())isr_de);
        set_gate(VEC_BP, (void (*)())isr_bp);
        set_gate(VEC_UD, (void (*)())isr_ud);
        set_gate(VEC_GP, (void (*)())isr_gp);
        set_gate(VEC_PF, (void (*)())isr_pf);

        // lidt
        g_idtr.limit = sizeof(g_idt) - 1;
        g_idtr.base = (uint64_t)&g_idt[0];
        asm volatile("lidt %0" ::"m"(g_idtr) : "memory");
    }

    void enable_breakpoint(bool on)
    {
        if (on)
        {
            asm volatile("int3");
        } // 今は簡易にその場でint3を打つ用途に
    }

} // namespace idt
