#pragma once

#include <stddef.h>

extern "C"
{
    void *memcpy(void *dest, const void *src, size_t n);
    void *memset(void *s, int c, size_t n);
}