#include "../include/framebuffer.hpp"
#include "graphic/window/window_manager.hpp"
#include "console.hpp"
#include "idt.hpp"
#include "io.hpp"
#include "painter.hpp"
#include "pic.hpp"

extern graphic::Window *g_mouse_cursor;

namespace
{
    static uint16_t g_cs_selector = 0;

    uint8_t g_mouse_packet[3];
    int g_mouse_packet_phase = 0;

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
        /*
        fb.fillRect(0, 0, fb.width(), 24, {160, 40, 40});
        uint32_t x = 0, y = 0;
        p.setColor({255, 255, 255});
        p.setTextLayout(8, 12);
        p.drawTextWrap(x, y, "EXCEPTION", fb.width() - 8);
        c.setColors({255, 255, 255}, {0, 0, 0});
        c.println("");
        c.print_bg(name, {0, 0, 0}, {255, 220, 40});
        c.println("");
        */
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
            c.println("Exception Handler - NMI");
            c.printf("RIP=%p  RFLAGS=0x%x\n",
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
        c.println("Exception Handler - Divide-by-Zero");
        c.printf("RIP=%p  CS=0x%x  RFLAGS=0x%x\n",
                 (void *)frame->rip, (unsigned)frame->cs, (unsigned long long)frame->rflags);
        for (;;)
            asm volatile("hlt");
    }

    __attribute__((interrupt)) static void isr_df(InterruptFrame *frame, uint64_t /*error*/)
    {
        // error code は常に 0
        print_panic_header("#DF Double Fault (IST1)");
        Framebuffer fb(*g_bi);
        Painter p(fb);
        Console c(fb, p);
        c.println("Exception Handler - Double Fault");
        c.printf("RIP=%p  RSP=%p  RFLAGS=0x%x\n",
                 (void *)frame->rip, (void *)frame->rsp, (unsigned long long)frame->rflags);
        c.println("Entered via IST1. System halted.");
        for (;;)
            asm volatile("hlt");
    }

    __attribute__((interrupt)) static void isr_bp(InterruptFrame *frame)
    {
        print_panic_header("#BP Breakpoint");
        Framebuffer fb(*g_bi);
        Painter p(fb);
        Console c(fb, p);
        c.println("Exception Handler - Breakpoint");
        c.printf("RIP=%p\n", (void *)frame->rip);
        for (;;)
            asm volatile("hlt");
    }

    __attribute__((interrupt)) static void isr_ud(InterruptFrame *frame)
    {
        print_panic_header("#UD Invalid opcode");
        Framebuffer fb(*g_bi);
        Painter p(fb);
        Console c(fb, p);
        c.println("Exception Handler - Invalid opcode");
        c.printf("RIP=%p  RFLAGS=0x%x\n",
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
        c.println("Exception Handler - General protection fault");
        c.printf("RIP=%p  ERR=0x%x  RFLAGS=0x%x\n",
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
        c.println("Exception Handler - Page fault");
        c.printf("RIP=%p  CR2=%p  ERR=0x%x\n",
                 (void *)frame->rip, (void *)cr2, (unsigned long long)error);
        c.println("ERR bits: P=1(not-present) | W=2(write) | U=4(user) | RSVD=8 | I=16(inst)");
        for (;;)
            asm volatile("hlt");
    }

    // EOI(End of Interrupt)をPICに送信する
    void notify_eoi(uint8_t irq)
    {
        if (irq >= 8)
        {
            outb(PIC1_OCW2, 0x60 | (irq & 0x07)); // スレーブには具体的なIRQ
            outb(PIC0_OCW2, 0x62);                // マスターにはIRQ2の完了を通知
        }
        else
        {
            outb(PIC0_OCW2, 0x60 | irq); // マスターのみ
        }
    }

    // C++側のキーボードハンドラ本体
    void KeyboardHandler(InterruptFrame *frame)
    {
        uint8_t scancode = inb(0x60);
        if (scancode < 0x80)
        {
            char key = SCANCODE_TO_ASCII[scancode];
            if (key != 0x00)
            {
                // (ここで、文字'key'をコンソールやアクティブなウィンドウに渡す処理を呼び出す)
                // 例: console.put_char(key);
            }
        }
        notify_eoi(1);
    }

    // C++側のマウスハンドラ本体
    void MouseHandler(InterruptFrame *frame)
    {
        uint8_t mouse_data = inb(0x60);
        if (g_mouse_packet_phase == 0)
        {
            // 1バイト目は、3ビット目が必ず1になる
            if ((mouse_data & 0x08) == 0)
            {
                // 同期がずれているのでリセット
                notify_eoi(12);
                return;
            }
            g_mouse_packet[0] = mouse_data;
            g_mouse_packet_phase = 1;
        }
        else if (g_mouse_packet_phase == 1)
        {
            g_mouse_packet[1] = mouse_data;
            g_mouse_packet_phase = 2;
        }
        else if (g_mouse_packet_phase == 2)
        {
            g_mouse_packet[2] = mouse_data;
            g_mouse_packet_phase = 0; // 次のパケットに備える

            // ===== 3バイト揃ったので、ここで解析処理を行う =====
            bool left_button = (g_mouse_packet[0] & 0x01) != 0;
            bool right_button = (g_mouse_packet[0] & 0x02) != 0;
            int32_t delta_x = g_mouse_packet[1];
            int32_t delta_y = g_mouse_packet[2];

            // 9ビット目(符号ビット)が立っている場合は負の値として拡張
            if ((g_mouse_packet[0] & 0x10) != 0)
            {
                delta_x |= 0xFFFFFF00;
            }
            if ((g_mouse_packet[0] & 0x20) != 0)
            {
                delta_y |= 0xFFFFFF00;
            }

            // Y軸の移動量は上下が逆なので符号を反転させるのが一般的
            delta_y = -delta_y;

            if (g_mouse_cursor)
            {
                auto &wm = graphic::WindowManager::GetInstance();
                auto clip = g_mouse_cursor->GetWindowClip();
                int new_x = clip.x + delta_x;
                int new_y = clip.y + delta_y;
                wm.MoveWindow(g_mouse_cursor, new_x, new_y);
            }
        }
        notify_eoi(12);
    }

    // キーボード割り込みハンドラ (IRQ 1)
    __attribute__((interrupt)) void isr_ps2_keyboard(InterruptFrame *frame)
    {
        KeyboardHandler(frame);
    }

    // マウス割り込みハンドラ (IRQ 12)
    __attribute__((interrupt)) void isr_ps2_mouse(InterruptFrame *frame)
    {
        MouseHandler(frame);
    }

} // anon

namespace idt
{

    void init(const BootInfo *bi)
    {
        g_bi = bi;

        asm volatile("mov %%cs, %0" : "=r"(g_cs_selector));

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

        set_gate(IRQ_MASTER_BASE + IRQ_KEYBOARD, (void (*)())isr_ps2_keyboard);
        set_gate(IRQ_MASTER_BASE + IRQ_MOUSE, (void (*)())isr_ps2_mouse);
    }

    void enable_breakpoint(bool on)
    {
        if (on)
        {
            asm volatile("int3");
        } // 今は簡易にその場でint3を打つ用途に
    }

    void install_double_fault(uint8_t ist_index)
    {
        asm volatile("mov %%cs, %0" : "=r"(g_cs_selector));
        set_gate(VEC_DF, (void (*)())isr_df, ist_index); // ← IST=1 を渡す
    }

} // namespace idt
