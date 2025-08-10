#pragma once
#include "painter.hpp"

class Console
{
public:
    Console(Painter &p) : p_(p), x_(8), y_(40), line_(10) {}
    void titleBar(Framebuffer &fb, const char *title)
    {
        fb.fillRect(0, 0, fb.width(), 24, {32, 120, 255});
        p_.drawText(8, 6, title);
    }
    void println(const char *s)
    {
        p_.drawText(x_, y_, s);
        y_ += line_;
    }
    void print_kv(const char *k, uint64_t v)
    {
        p_.drawText(x_, y_, k);
        p_.drawDec(x_ + 9 * 10, y_, v);
        y_ += line_;
    }

private:
    Painter &p_;
    uint32_t x_, y_, line_;
};
