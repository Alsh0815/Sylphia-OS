#include "FontEngine.hpp"

const uint8_t *GetFont(char c);

const uint32_t *Char2Bmp(const char c, uint32_t *buf)
{
    const uint8_t *font = GetFont(c);
    if (!font)
        return nullptr;

    for (int dy = 0; dy < 16; ++dy)
    {
        uint8_t row_bitmap = font[dy];

        for (int dx = 0; dx < 8; ++dx)
        {
            if (dx >= 8)
                break;

            uint32_t index = dy * 8 + dx;
            if ((row_bitmap >> (7 - dx)) & 1)
            {
                buf[index] = 0xFFFFFFFF;
            }
            else
            {
                buf[index] = 0x00000000;
            }
        }
    }
    return buf;
}
const uint32_t *Str2Bmp(const char *str, uint32_t *buf)
{
    while (*str)
    {
        Char2Bmp(*str, buf);
        str++;
        buf += 8 * 16;
    }
    return buf;
}
