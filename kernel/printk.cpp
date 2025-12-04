#include "printk.hpp"
#include "console.hpp"
#include <stdarg.h>
#include <stdint.h>

// 数値を文字列に変換するヘルパー関数（符号なし版）
// value: 変換する数値
// buffer: 格納先
// base: 基数 (10 or 16)
static void utoa(unsigned long long value, char *str, int base)
{
    char *ptr = str;
    char *ptr1 = str;
    char tmp_char;

    // 桁ごとに文字へ変換 (逆順に入る)
    do
    {
        int remainder = value % base;
        *ptr++ = (remainder < 10) ? (remainder + '0') : (remainder - 10 + 'a');
    } while (value /= base);

    // 終端
    *ptr-- = '\0';

    // 逆順になっているので反転させる
    while (ptr1 < ptr)
    {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }
}

// 文字列の長さを取得
static int strlen_local(const char *s)
{
    int len = 0;
    while (*s++)
        len++;
    return len;
}

extern "C" int kprintf(const char *format, ...)
{
    if (!g_console)
        return -1; // コンソール未初期化なら何もしない

    va_list args;
    va_start(args, format);

    char buffer[1024]; // 1行分のバッファ
    char num_buf[32];  // 数値変換用
    int buf_idx = 0;

    // バッファに1文字追加するヘルパー
    auto putc = [&](char c)
    {
        if (buf_idx >= 1000)
        {
            buffer[buf_idx] = '\0';
            g_console->PutString(buffer);
            buf_idx = 0;
        }
        buffer[buf_idx++] = c;
    };

    while (*format)
    {
        if (*format == '%')
        {
            format++;

            // フラグ解析
            bool zero_pad = false;
            if (*format == '0')
            {
                zero_pad = true;
                format++;
            }

            // 幅解析
            int width = 0;
            while (*format >= '0' && *format <= '9')
            {
                width = width * 10 + (*format - '0');
                format++;
            }

            // サイズ修飾子の解析 (l, ll)
            int long_mode = 0; // 0:int, 1:long, 2:long long
            if (*format == 'l')
            {
                long_mode = 1;
                format++;
                if (*format == 'l')
                {
                    long_mode = 2;
                    format++;
                }
            }

            switch (*format)
            {
                case 'd':
                {
                    long long val;
                    if (long_mode > 0)
                        val = va_arg(args, long long);
                    else
                        val = va_arg(args, int);

                    bool negative = (val < 0);
                    if (negative)
                        val = -val;

                    utoa((unsigned long long)val, num_buf, 10);

                    int num_len = strlen_local(num_buf);
                    int total_len = num_len + (negative ? 1 : 0);
                    int pad_len = (width > total_len) ? (width - total_len) : 0;

                    if (!zero_pad)
                    {
                        // スペース埋め（符号の前）
                        for (int i = 0; i < pad_len; i++)
                            putc(' ');
                    }

                    if (negative)
                        putc('-');

                    if (zero_pad)
                    {
                        // ゼロ埋め（符号の後）
                        for (int i = 0; i < pad_len; i++)
                            putc('0');
                    }

                    for (char *p = num_buf; *p; p++)
                        putc(*p);
                    break;
                }
                case 'u':
                {
                    unsigned long long val;
                    if (long_mode > 0)
                        val = va_arg(args, unsigned long long);
                    else
                        val = va_arg(args, unsigned int);

                    utoa(val, num_buf, 10);

                    int num_len = strlen_local(num_buf);
                    int pad_len = (width > num_len) ? (width - num_len) : 0;

                    char pad_char = zero_pad ? '0' : ' ';
                    for (int i = 0; i < pad_len; i++)
                        putc(pad_char);

                    for (char *p = num_buf; *p; p++)
                        putc(*p);
                    break;
                }
                case 'x':
                {
                    unsigned long long val;
                    if (long_mode > 0)
                        val = va_arg(args, unsigned long long);
                    else
                        val = va_arg(args, unsigned int);

                    utoa(val, num_buf, 16);

                    int num_len = strlen_local(num_buf);
                    int pad_len = (width > num_len) ? (width - num_len) : 0;

                    char pad_char = zero_pad ? '0' : ' ';
                    for (int i = 0; i < pad_len; i++)
                        putc(pad_char);

                    for (char *p = num_buf; *p; p++)
                        putc(*p);
                    break;
                }
                case 'p':
                {
                    // ポインタは常に 0x 付き
                    putc('0');
                    putc('x');

                    unsigned long long val = va_arg(args, unsigned long long);
                    utoa(val, num_buf, 16);

                    // %p の場合、幅指定は 0x を含まない数値部分に適用
                    int num_len = strlen_local(num_buf);
                    int pad_len =
                        (width > num_len + 2) ? (width - num_len - 2) : 0;

                    for (int i = 0; i < pad_len; i++)
                        putc('0');

                    for (char *p = num_buf; *p; p++)
                        putc(*p);
                    break;
                }
                case 's':
                {
                    const char *s = va_arg(args, const char *);
                    if (!s)
                        s = "(null)";

                    int str_len = strlen_local(s);
                    int pad_len = (width > str_len) ? (width - str_len) : 0;

                    // 文字列は常にスペース埋め
                    for (int i = 0; i < pad_len; i++)
                        putc(' ');

                    while (*s)
                        putc(*s++);
                    break;
                }
                case 'c':
                {
                    char c = (char)va_arg(args, int);

                    int pad_len = (width > 1) ? (width - 1) : 0;
                    for (int i = 0; i < pad_len; i++)
                        putc(' ');

                    putc(c);
                    break;
                }
                case '%':
                    putc('%');
                    break;
                default:
                    // 非対応フォーマット
                    putc('%');
                    putc(*format);
                    break;
            }
        }
        else
        {
            putc(*format);
        }
        format++;
    }

    // 残りを出力
    buffer[buf_idx] = '\0';
    g_console->PutString(buffer);

    va_end(args);
    return buf_idx;
}