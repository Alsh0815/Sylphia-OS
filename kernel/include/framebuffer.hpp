#pragma once
#include <stdint.h>
#include "bootinfo.h"

struct Color
{
    uint8_t r, g, b;
};

class Framebuffer
{
public:
    Framebuffer(const BootInfo &bi)
        : base_(reinterpret_cast<volatile uint32_t *>(static_cast<uintptr_t>(bi.fb_base))),
          w_(bi.width), h_(bi.height), pitch_(bi.pitch), bgr_(bi.pixel_format != 0) {}
    uint32_t width() const { return w_; }
    uint32_t height() const { return h_; }
    void clear(Color c) { fillRect(0, 0, w_, h_, c); }
    void fillRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, Color c)
    {
        uint32_t px = pack(c);
        uint32_t x1 = (x + w > w_) ? w_ : x + w, y1 = (y + h > h_) ? h_ : y + h;
        for (uint32_t yy = y; yy < y1; ++yy)
        {
            volatile uint32_t *row = base_ + yy * pitch_;
            for (uint32_t xx = x; xx < x1; ++xx)
                row[xx] = px;
        }
    }
    void putPixel(uint32_t x, uint32_t y, Color c)
    {
        if (x >= w_ || y >= h_)
            return;
        base_[y * pitch_ + x] = pack(c);
    }

private:
    uint32_t pack(Color c) const
    {
        return bgr_ ? (c.b | (c.g << 8) | (c.r << 16)) : (c.r | (c.g << 8) | (c.b << 16));
    }
    volatile uint32_t *base_;
    uint32_t w_, h_, pitch_;
    bool bgr_;
};
