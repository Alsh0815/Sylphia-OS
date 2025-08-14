#pragma once

#include <stddef.h>

extern "C" void *memset(void *p, int c, size_t n)
{
    unsigned char *b = (unsigned char *)p;
    for (size_t i = 0; i < n; i++)
        b[i] = (unsigned char)c;
    return p;
}
extern "C" void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *dd = (unsigned char *)d;
    const unsigned char *ss = (const unsigned char *)s;
    for (size_t i = 0; i < n; i++)
        dd[i] = ss[i];
    return d;
}
extern "C" void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *dd = (unsigned char *)d;
    const unsigned char *ss = (const unsigned char *)s;
    if (dd < ss)
    {
        for (size_t i = 0; i < n; i++)
            dd[i] = ss[i];
    }
    else
    {
        for (size_t i = n; i > 0; i--)
            dd[i - 1] = ss[i - 1];
    }
    return d;
}
extern "C" int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    for (size_t i = 0; i < n; i++)
    {
        if (p[i] != q[i])
            return (int)p[i] - (int)q[i];
    }
    return 0;
}
