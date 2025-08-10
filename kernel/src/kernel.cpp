#include <stdint.h>
#include "bootinfo.h"
#include "font8x8.h"

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
    // 簡易ガード
    if (!bi || !bi->fb_base || bi->width == 0 || bi->height == 0)
    {
        for (;;)
            __asm__ __volatile__("hlt");
    }

    const uint32_t w = bi->width, h = bi->height, pitch = bi->pitch;
    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)bi->fb_base;

    auto make_color = [&](uint8_t r, uint8_t g, uint8_t b) -> uint32_t
    {
        if (bi->pixel_format == 0 /*RGB*/)
        {
            return (r) | (g << 8) | (b << 16);
        }
        else
        { /* BGR が多い */
            return (b) | (g << 8) | (r << 16);
        }
    };

    // 背景塗りつぶし（深い紺）
    const uint32_t bg = make_color(10, 12, 24);
    for (uint32_t y = 0; y < h; y++)
    {
        uint32_t *row = (uint32_t *)(&fb[y * pitch]);
        for (uint32_t x = 0; x < w; x++)
            row[x] = bg;
    }

    // 矩形ユーティリティ
    auto fill_rect = [&](uint32_t x0, uint32_t y0, uint32_t ww, uint32_t hh, uint32_t color)
    {
        uint32_t x1 = (x0 + ww > w) ? w : x0 + ww;
        uint32_t y1 = (y0 + hh > h) ? h : y0 + hh;
        for (uint32_t y = y0; y < y1; ++y)
        {
            uint32_t *row = (uint32_t *)(&fb[y * pitch]);
            for (uint32_t x = x0; x < x1; ++x)
                row[x] = color;
        }
    };

    // 文字描画（8x8, 1px間隔）
    auto put_char = [&](uint32_t x, uint32_t y, char c, uint32_t fg)
    {
        const Glyph *g = font_lookup(c);
        for (int r = 0; r < 8; r++)
        {
            uint8_t bits = g->rows[r];
            uint32_t *row = (uint32_t *)(&fb[(y + r) * pitch]);
            for (int col = 0; col < 8; ++col)
            {
                if (bits & (0x80 >> col))
                    row[x + col] = fg;
            }
        }
    };
    auto put_text = [&](uint32_t x, uint32_t y, const char *s, uint32_t fg)
    {
        uint32_t cx = x;
        while (*s)
        {
            if (*s == '\n')
            {
                y += 10;
                cx = x;
                ++s;
                continue;
            }
            put_char(cx, y, *s, fg);
            cx += 9;
            ++s;
        }
    };

    // タイトルバー
    const uint32_t accent = make_color(32, 120, 255);
    fill_rect(0, 0, w, 24, accent);

    const uint32_t fg = make_color(255, 255, 255);
    put_text(8, 6, "SYLPHIA OS (kernel-elf64-min)", fg);

    put_text(8, 40, "Framebuffer:", fg);
    put_text(8, 52, "W x H =", fg);
    auto digits = [](uint32_t v) -> int
    { int d=1; while(v>=10){ v/=10; ++d; } return d; };
    auto print_u32 = [&](uint32_t x, uint32_t y, uint32_t v)
    {
        char buf[12];
        int i = 0;
        if (v == 0)
        {
            buf[i++] = '0';
        }
        while (v)
        {
            buf[i++] = '0' + (v % 10);
            v /= 10;
        }
        for (int j = i - 1; j >= 0; --j)
            put_char(x + (i - 1 - j) * 9, y, buf[j], fg);
    };
    auto print_u64 = [&](uint32_t x, uint32_t y, uint64_t v)
    {
        char buf[24];
        int i = 0;
        if (v == 0)
        {
            buf[i++] = '0';
        }
        while (v)
        {
            buf[i++] = char('0' + (v % 10));
            v /= 10;
        }
        for (int j = i - 1; j >= 0; --j)
            put_char(x + (i - 1 - j) * 9, y, buf[j], fg);
    };

    print_u32(8 + 9 * 8, 52, w);
    uint32_t x_after_w = 8 + 9 * 8 + 9 * digits(w);
    put_char(x_after_w, 52, 'x', fg);
    print_u32(x_after_w + 9, 52, h);

    put_text(8, 72, "MEMORY MAP:", fg);

    if (bi->mmap_ptr && bi->mmap_size && bi->mmap_desc_size)
    {
        auto *base = (const uint8_t *)(uintptr_t)bi->mmap_ptr;
        uint64_t total_pages_conv = 0;
        uint32_t entries = (uint32_t)(bi->mmap_size / bi->mmap_desc_size);

        for (uint32_t i = 0; i < entries; ++i)
        {
            auto *d = (const EFIMemoryDescriptor *)(base + (uint64_t)i * bi->mmap_desc_size);
            if (d->Type == EfiConventionalMemory ||
                d->Type == EfiBootServicesCode ||
                d->Type == EfiBootServicesData)
            {
                total_pages_conv += d->NumberOfPages;
            }
        }

        put_text(8, 84, "entries :", fg);
        print_u32(8 + 9 * 10, 84, entries);

        put_text(8, 96, "RAM(MiB):", fg);
        /* 1 page = 4096 bytes */
        uint64_t mib = (total_pages_conv * 4096ULL) >> 20;
        print_u64(8 + 9 * 10, 96, mib);
    }
    else
    {
        put_text(8, 84, "no memory map", fg);
    }

    for (;;)
        __asm__ __volatile__("hlt");
}
