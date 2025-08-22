#pragma once
#include <cstdarg>
#include <cstdint>
#include "painter.hpp"

class Console
{
public:
    Console(Framebuffer &fb, Painter &p)
        : fb_(fb), p_(p), x_(8), y_(40), lineH_(12)
    {
        p_.setTextLayout(8, lineH_);
        fb_.resetClip();
        // 画面下 8px をマージン（スクロール時の見切れ対策）
        clip_ = {8, 32, fb_.width() - 16, fb_.height() - 40};
        fb_.setClip(clip_);
    }

    void clear_fullscreen(Color bg = {10, 12, 24}, bool reset_clip = true);
    void setColor(Color fg) { p_.setColor(fg); }
    void setColors(Color fg, Color bg) { p_.setColors(fg, bg); }
    void noBackground() { p_.disableBackground(); }
    void setClip(Clip c)
    {
        clip_ = c;
        fb_.setClip(clip_);
    }

    void clear(Color bg = {0, 0, 0})
    {
        fb_.fillRect(clip_.x, clip_.y, clip_.w, clip_.h, bg);
        x_ = clip_.x;
        y_ = clip_.y;
    }

    void print(const char *s)
    {
        uint32_t right = clip_.x + clip_.w;
        p_.drawTextWrap(x_, y_, s, right);
        ensureScroll();
    }

    void printf(const char *fmt, ...);

    void println(const char *s)
    {
        print(s);
        newline();
    }

    void print_bg(const char *s, Color fg, Color bg)
    {
        uint32_t right = clip_.x + clip_.w;
        uint32_t x = x_, y = y_;
        p_.drawTextWrapBg(x, y, s, right, fg, bg);
        x_ = x;
        y_ = y;
        newline();
    }

    void print_kv(const char *k, uint64_t v)
    {
        uint32_t right = clip_.x + clip_.w;
        p_.drawTextWrap(x_, y_, k, right);
        p_.drawTextWrap(x_, y_, ": ", right);
        p_.drawDec(x_, y_, v, right);
        newline();
    }

    void print_uint(uint64_t val, int base, bool zero_pad, int width);

    void newline()
    {
        x_ = clip_.x;
        y_ += lineH_;
        ensureScroll();
    }

    void vprintf(const char *fmt, va_list args);

private:
    Framebuffer &fb_;
    Painter &p_;
    Clip clip_;
    uint32_t x_, y_, lineH_;

    void ensureScroll()
    {
        uint32_t bottom = clip_.y + clip_.h;
        if (y_ + Painter::kCH > bottom)
        {
            fb_.scrollUp(clip_.y, clip_.h, lineH_);
            y_ -= lineH_;
        }
    }

    void put_char(char c);
};
