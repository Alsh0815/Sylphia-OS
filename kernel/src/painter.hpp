#pragma once
#include <stdint.h>
#include "../include/framebuffer.hpp"
#include "../include/font8x8.hpp"

class Painter
{
public:
    Painter(Framebuffer &fb) : fb_(fb), fg_{255, 255, 255} {}
    void setColor(Color c) { fg_ = c; }
    void drawChar(uint32_t x, uint32_t y, char c)
    {
        const Glyph *g = font_lookup(c);
        for (int r = 0; r < 8; r++)
        {
            uint8_t bits = g->rows[r];
            for (int col = 0; col < 8; ++col)
                if (bits & (0x80 >> col))
                    fb_.putPixel(x + col, y + r, fg_);
        }
    }
    void drawText(uint32_t x, uint32_t y, const char *s)
    {
        uint32_t cx = x, cy = y;
        while (*s)
        {
            if (*s == '\n')
            {
                cy += 10;
                cx = x;
                ++s;
                continue;
            }
            drawChar(cx, cy, *s++);
            cx += 9;
        }
    }
    void drawDec(uint32_t x, uint32_t y, uint64_t v)
    {
        char buf[24];
        int i = 0;
        if (v == 0)
            buf[i++] = '0';
        while (v)
        {
            buf[i++] = char('0' + (v % 10));
            v /= 10;
        }
        for (int j = i - 1; j >= 0; --j)
            drawChar(x + (i - 1 - j) * 9, y, buf[j]);
    }

private:
    Framebuffer &fb_;
    Color fg_;
};
