#pragma once

#include <cstddef>

inline size_t strlen(const char *s)
{
    if (!s)
        return 0;
    size_t n = 0;
    while (s[n] != '\0')
        ++n;
    return n;
}

inline int strcmp(const char *s1, const char *s2)
{
    while (*s1 != '\0' && *s1 == *s2)
    {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

inline char *strncpy(char *dest, const char *src, size_t n)
{
    if (!dest || !src)
        return dest;

    size_t i;
    for (i = 0; i < n && src[i] != '\0'; ++i)
    {
        dest[i] = src[i];
    }
    for (; i < n; ++i)
    {
        dest[i] = '\0';
    }

    return dest;
}