#pragma once
#include <stdint.h>
#include "bootinfo.h"

struct Color
{
    uint8_t r, g, b;
};

struct Clip
{
    uint32_t x = 0, y = 0, w = 0, h = 0; // w,h=0 なら全画面
};

class Framebuffer
{
public:
    Framebuffer(const BootInfo &bi)
        : base_(reinterpret_cast<volatile uint32_t *>((uintptr_t)bi.fb_base)),
          w_(bi.width), h_(bi.height), pitch_(bi.pitch), bgr_(bi.pixel_format != 0) {}
    Framebuffer(volatile uint32_t *base, uint32_t w, uint32_t h, uint32_t pitch)
        : base_(base), w_(w), h_(h), pitch_(pitch), bgr_(false)
    {
        resetClip();
    }

    uint32_t width() const { return w_; }
    uint32_t height() const { return h_; }

    void setClip(Clip c) { clip_ = normalize(c); }
    void resetClip() { clip_ = {0, 0, w_, h_}; }

    void clear(Color c) { fillRect(0, 0, w_, h_, c); }

    void fillRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, Color c)
    {
        if (w == 0 || h == 0)
            return;
        auto r = intersect({x, y, w, h}, clip_);
        if (r.w == 0 || r.h == 0)
            return;
        const uint32_t px = pack(c);
        for (uint32_t yy = r.y; yy < r.y + r.h; ++yy)
        {
            volatile uint32_t *row = base_ + yy * pitch_;
            for (uint32_t xx = r.x; xx < r.x + r.w; ++xx)
                row[xx] = px;
        }
    }

    void putPixel(uint32_t x, uint32_t y, Color c)
    {
        if (!insideClip(x, y))
            return;
        base_[y * pitch_ + x] = pack(c);
    }

    void putPixel(uint32_t x, uint32_t y, uint32_t packed_color)
    {
        if (!insideClip(x, y))
            return;
        base_[y * pitch_ + x] = packed_color;
    }

    // 1行分を上にスクロール（dest_y ← src_y）
    void scrollUp(uint32_t y0, uint32_t height, uint32_t lines)
    {
        if (lines == 0 || height == 0)
            return;
        if (lines >= height)
        { // 全消去
            fillRect(clip_.x, y0, clip_.w, height, {0, 0, 0});
            return;
        }
        const uint32_t bytesPerPix = 4;
        const uint32_t rowBytes = clip_.w * bytesPerPix;

        // 上へコピー
        for (uint32_t yy = 0; yy < height - lines; ++yy)
        {
            volatile uint32_t *d = base_ + (y0 + yy) * pitch_ + clip_.x;
            volatile uint32_t *s = base_ + (y0 + yy + lines) * pitch_ + clip_.x;
            // memmove等は使わない（自前ループ）
            for (uint32_t bx = 0; bx < clip_.w; ++bx)
                d[bx] = s[bx];
        }
        // 下端 lines 行を消去
        fillRect(clip_.x, y0 + (height - lines), clip_.w, lines, {0, 0, 0});
    }

private:
    volatile uint32_t *base_;
    uint32_t w_, h_, pitch_;
    bool bgr_;
    Clip clip_{0, 0, 0, 0};

    uint32_t pack(Color c) const
    {
        return bgr_ ? (c.b | (c.g << 8) | (c.r << 16)) : (c.r | (c.g << 8) | (c.b << 16));
    }
    bool insideClip(uint32_t x, uint32_t y) const
    {
        const Clip &r = clip_;
        return (x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h);
    }
    static Clip intersect(Clip a, Clip b)
    {
        if (a.w == 0 || a.h == 0)
            return {0, 0, 0, 0};
        if (b.w == 0 || b.h == 0)
            return a;
        uint32_t x1 = a.x > a.y ? 0 : 0; // dummy to silence warnings (no std)
        (void)x1;
        uint32_t ax2 = a.x + a.w, ay2 = a.y + a.h;
        uint32_t bx2 = b.x + b.w, by2 = b.y + b.h;
        Clip r;
        r.x = (a.x > b.x) ? a.x : b.x;
        r.y = (a.y > b.y) ? a.y : b.y;
        uint32_t rx2 = (ax2 < bx2) ? ax2 : bx2;
        uint32_t ry2 = (ay2 < by2) ? ay2 : by2;
        r.w = (rx2 > r.x) ? (rx2 - r.x) : 0;
        r.h = (ry2 > r.y) ? (ry2 - r.y) : 0;
        return r;
    }
    Clip normalize(Clip c) const
    {
        Clip r = c;
        if (r.w == 0 || r.h == 0)
        {
            r = {0, 0, w_, h_};
            return r;
        }
        if (r.x >= w_)
            r.x = w_ - 1;
        if (r.y >= h_)
            r.y = h_ - 1;
        if (r.x + r.w > w_)
            r.w = w_ - r.x;
        if (r.y + r.h > h_)
            r.h = h_ - r.y;
        return r;
    }
};
