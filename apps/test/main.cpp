#include "../_header/syscall.hpp"

// --- 簡易ユーティリティ関数 (ユーザーランド用) ---

int strlen(const char *s)
{
    int len = 0;
    while (s[len])
        len++;
    return len;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

void print(const char *s)
{
    while (*s)
        PutChar(*s++);
}

// Recursive print_int to avoid stack buffer and divisor overflow issues
void print_int(int n)
{
    if (n < 0)
    {
        PutChar('-');
        n = -n;
    }
    if (n / 10)
    {
        print_int(n / 10);
    }
    PutChar((n % 10) + '0');
}

// Literal-free print_hex to avoid .rodata issues
void print_hex(uint64_t n)
{
    PutChar('0');
    PutChar('x');
    for (int i = 15; i >= 0; --i)
    {
        int nibble = (n >> (i * 4)) & 0xF;
        if (nibble < 10)
        {
            PutChar(nibble + '0');
        }
        else
        {
            PutChar(nibble - 10 + 'A');
        }
    }
}

extern "C" int main(int argc, char **argv)
{
    // 文字列リテラルを一切使わない最小テスト
    // 'H', 'i', '!', '\n' を直接出力
    PutChar('H');
    PutChar('i');
    PutChar('!');
    PutChar('\n');
    print(".asm -> .s");

    // Exitを呼ぶ
    Exit();
    return 0;
}