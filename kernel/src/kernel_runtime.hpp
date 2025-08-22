#pragma once

#include <cstddef>
#include <cstdint>

template <typename F>
struct ScopeExit
{
    F f;
    ScopeExit(F f) : f(f) {}
    ~ScopeExit() { f(); }
};

extern "C" void *memset(void *p, int c, size_t n);
extern "C" void *memcpy(void *d, const void *s, size_t n);
extern "C" void *memmove(void *d, const void *s, size_t n);
extern "C" int memcmp(const void *a, const void *b, size_t n);

void simple_wait(uint64_t n);