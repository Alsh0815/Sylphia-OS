#include <stdint.h>
#include "../include/bootinfo.h"
#include "../include/framebuffer.hpp"
#include "../include/font8x8.hpp"
#include "painter.hpp"
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
    Console con(paint);

    con.titleBar(fb, "SYLPHIA OS (renderer v0)");

    con.println("Framebuffer:");
    con.print_kv("W", bi->width);
    con.print_kv("H", bi->height);
    con.print_kv("Pitch", bi->pitch);

    for (;;)
        __asm__ __volatile__("hlt");
}
