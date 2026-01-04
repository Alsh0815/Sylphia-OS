#pragma once

#include <stdint.h>

namespace Debug
{
namespace Serial
{
void Out(const char c);
void Out(const char *str);

// 16進数出力ヘルパー
inline void OutHex(uint64_t value)
{
    const char *hex = "0123456789ABCDEF";
    char buf[17];
    buf[16] = '\0';
    for (int i = 15; i >= 0; --i)
    {
        buf[i] = hex[value & 0xF];
        value >>= 4;
    }
    Out("0x");
    Out(buf);
}

// 10進数出力ヘルパー
inline void OutDec(uint32_t value)
{
    if (value == 0)
    {
        Out('0');
        return;
    }
    char buf[12];
    int i = 0;
    while (value > 0)
    {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }
    while (i > 0)
        Out(buf[--i]);
}
} // namespace Serial
} // namespace Debug