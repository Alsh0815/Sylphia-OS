#include "console.hpp"

void Console::clear_fullscreen(Color bg, bool reset_clip)
{
    // フレームバッファ全域をクリア
    fb_.fillRect(0, 0, fb_.width(), fb_.height(), bg);

    // クリップを全画面に戻してから、既定のログエリアに再設定
    if (reset_clip)
    {
        fb_.resetClip();
        clip_ = {8, 32, fb_.width() - 16, fb_.height() - 40}; // 既存のレイアウトに合わせる
        fb_.setClip(clip_);
    }

    // コンソールのカーソル位置を原点へ
    x_ = clip_.x;
    y_ = clip_.y;
}

void Console::vprintf(const char *fmt, va_list args)
{
    for (const char *p = fmt; *p; ++p)
    {
        if (*p != '%')
        {
            put_char(*p);
            continue;
        }
        ++p;
        switch (*p)
        {
        case 's':
        {
            const char *str = va_arg(args, const char *);
            while (*str)
                put_char(*str++);
            break;
        }
        case 'd':
        case 'i':
        {
            int val = va_arg(args, int);
            if (val < 0)
            {
                put_char('-');
                val = -val;
            }
            print_uint(val, 10);
            break;
        }
        case 'u':
        {
            unsigned int val = va_arg(args, unsigned int);
            print_uint(val, 10);
            break;
        }
        case 'x':
        case 'X':
        {
            unsigned int val = va_arg(args, unsigned int);
            print_uint(val, 16);
            break;
        }
        case 'p':
        {
            uintptr_t ptr = (uintptr_t)va_arg(args, void *);
            put_char('0');
            put_char('x');
            print_uint(ptr, 16);
            break;
        }
        case '%':
            put_char('%');
            break;
        default:
            put_char('%');
            put_char(*p);
            break;
        }
    }
}

void Console::put_char(char c)
{
    if (c == '\n')
    {
        newline();
        return;
    }

    // クリップ領域の右端を計算
    uint32_t right = clip_.x + clip_.w;

    // 1文字描画
    p_.drawChar(x_, y_, c);

    // カーソル移動
    x_ += Painter::kAdv;
    if (x_ + Painter::kCW > right)
    {
        newline();
    }
}

void Console::printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

void Console::print_uint(uint64_t val, int base)
{
    char buf[32];
    int idx = 0;
    do
    {
        int digit = val % base;
        buf[idx++] = (digit < 10) ? '0' + digit : 'a' + (digit - 10);
        val /= base;
    } while (val);
    for (int i = idx - 1; i >= 0; --i)
        put_char(buf[i]);
}