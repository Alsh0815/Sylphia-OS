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