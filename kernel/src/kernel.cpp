#include <stdint.h>
#include "../include/bootinfo.h"
#include "../include/framebuffer.hpp"
#include "../include/font8x8.hpp"
#include "painter.hpp"
#include "paging.hpp"
#include "console.hpp"

struct EFIMemoryDescriptor
{
    uint32_t Type;
    uint32_t Pad;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
};

enum
{
    EfiBootServicesCode = 3,
    EfiBootServicesData = 4,
    EfiConventionalMemory = 7 /* boot_services.h の列挙と合わせる */
};

extern "C" __attribute__((sysv_abi)) void kernel_main(BootInfo *bi)
{
    if (!bi || !bi->fb_base || bi->width == 0 || bi->height == 0)
        for (;;)
            __asm__ __volatile__("hlt");

    Framebuffer fb(*bi);
    fb.clear({10, 12, 24});

    Painter paint(fb);
    Console con(fb, paint);

    // タイトル
    fb.fillRect(0, 0, fb.width(), 24, {32, 120, 255});
    paint.setColor({255, 255, 255});
    uint32_t tx = 8, ty = 6;
    uint32_t right = fb.width() - 8;
    paint.setTextLayout(8, 12);
    paint.drawTextWrap(tx, ty, "SYLPHIA OS (text-color-clip)", right);

    con.setColors({255, 255, 255}, {0, 0, 0});
    con.printf("Version: v.%d.%d.%d.%d\n", 0, 1, 0, 4);
    con.println("Framebuffer Info:");
    con.print_kv("W", bi->width);
    con.print_kv("H", bi->height);
    con.print_kv("Pitch", bi->pitch);

    con.print_bg(
        "Highlighted long line with background will wrap seamlessly across the clip area.",
        /*fg*/ {0, 0, 0},
        /*bg*/ {255, 220, 40});

    ((volatile uint32_t *)(uintptr_t)bi->fb_base)[0] = 0x00FFFF;
    uint64_t cr3 = paging::init_identity(*bi);
    con.printf("Paging: CR3=0x%p, mapped up to %u MiB\n",
               (void *)cr3, (unsigned)((paging::mapped_limit() >> 20)));

    // ここで低位スタックを確保して移行（もう新CR3なので確実にマップ済み）
    paging::init_allocator(*bi);
    void *new_sp = paging::alloc_low_stack(16 * 4096); // 64KiB くらい
    if (new_sp)
    {
        uintptr_t sp = ((uintptr_t)new_sp) & ~0xFULL; // 16B アライン
        // RDI に bi（SysV ABIの第1引数）を積んで、新スタックで kernel_after_stack へ
        asm volatile(
            "mov %0, %%rsp\n\t"
            "xor %%rbp, %%rbp\n\t"        // フレームポインタ無効化（お守り）
            "call kernel_after_stack\n\t" // ← 戻らない設計
            :
            : "r"(sp), "D"(bi)
            : "memory");
        __builtin_unreachable(); // ここには来ない想定
    }

    for (;;)
        __asm__ __volatile__("hlt");
}

// kernel.cpp
extern "C" __attribute__((sysv_abi)) void kernel_after_stack(BootInfo *bi)
{
    // ここは「新しいスタック」上。ローカルを作り直してOK
    Framebuffer fb(*bi);
    Painter paint(fb);
    Console con(fb, paint);

    con.clear_fullscreen();
    con.println("Switched to low stack.");
    con.printf("Paging: mapped up to %u MiB\n",
               (unsigned)(paging::mapped_limit() >> 20));

    con.println("Fin.");
    for (;;)
        __asm__ __volatile__("hlt");
}
