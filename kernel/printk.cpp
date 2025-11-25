#include <stdarg.h>
#include <stdint.h>
#include "console.hpp"
#include "printk.hpp"

// 数値を文字列に変換するヘルパー関数
// value: 変換する数値
// buffer: 格納先
// base: 基数 (10 or 16)
void itoa(long long value, char *str, int base)
{
    char *ptr = str;
    char *ptr1 = str;
    char tmp_char;
    long long tmp_value;

    // 10進数の負の数対応
    if (base == 10 && value < 0)
    {
        *ptr++ = '-';
        str++; // 符号分進める
        value = -value;
    }

    tmp_value = value;

    // 桁ごとに文字へ変換 (逆順に入る)
    do
    {
        int remainder = tmp_value % base;
        *ptr++ = (remainder < 10) ? (remainder + '0') : (remainder - 10 + 'a');
    } while (tmp_value /= base);

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

extern "C" int kprintf(const char *format, ...)
{
    if (!g_console)
        return -1; // コンソール未初期化なら何もしない

    va_list args;
    va_start(args, format);

    char buffer[1024]; // 1行分のバッファ
    char num_buf[32];  // 数値変換用
    int buf_idx = 0;

    while (*format)
    {
        // バッファがいっぱいになりそうなら強制出力
        if (buf_idx > 1000)
        {
            buffer[buf_idx] = '\0';
            g_console->PutString(buffer);
            buf_idx = 0;
        }

        if (*format == '%')
        {
            format++; // '%' の次の文字へ
            switch (*format)
            {
            case 'd': // 整数 (int)
            {
                int val = va_arg(args, int);
                itoa(val, num_buf, 10);
                for (char *p = num_buf; *p; p++)
                    buffer[buf_idx++] = *p;
                break;
            }
            case 'x': // 16進数 (int)
            {
                int val = va_arg(args, int); // unsigned intとして扱うべきだが簡易的に
                // 0x を付ける
                buffer[buf_idx++] = '0';
                buffer[buf_idx++] = 'x';
                // 符号なしとして扱うためキャスト
                itoa((unsigned int)val, num_buf, 16);
                for (char *p = num_buf; *p; p++)
                    buffer[buf_idx++] = *p;
                break;
            }
            case 's': // 文字列
            {
                const char *s = va_arg(args, const char *);
                if (!s)
                    s = "(null)";
                while (*s)
                    buffer[buf_idx++] = *s++;
                break;
            }
            case 'c': // 文字
            {
                // varargsではcharはintに昇格される
                char c = (char)va_arg(args, int);
                buffer[buf_idx++] = c;
                break;
            }
            case '%': // %% -> %
                buffer[buf_idx++] = '%';
                break;
            default: // 非対応のフォーマットはそのまま出す
                buffer[buf_idx++] = '%';
                buffer[buf_idx++] = *format;
                break;
            }
        }
        else
        {
            // 普通の文字
            buffer[buf_idx++] = *format;
        }
        format++;
    }

    // 残りを出力
    buffer[buf_idx] = '\0';
    g_console->PutString(buffer);

    va_end(args);
    return buf_idx;
}