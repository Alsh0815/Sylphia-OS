#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(__x86_64__)

#define Hlt() __asm__ volatile("hlt")
#define CLI() __asm__ volatile("cli")
#define STI() __asm__ volatile("sti")
#define PAUSE() __asm__ volatile("pause");
#define WBINVD() __asm__ volatile("wbinvd");
#define DSB() __asm__ volatile("mfence");
#define ISB() __asm__ volatile("lfence");

#elif defined(__aarch64__)

#define Hlt() __asm__ volatile("wfi")
#define CLI() __asm__ volatile("msr daifset, #2")
#define STI() __asm__ volatile("msr daifclr, #2")
#define PAUSE() __asm__ volatile("yield");
#define WBINVD() __asm__ volatile("tlbi vmalle1is");
#define DSB() __asm__ volatile("dsb sy" ::: "memory");
#define ISB() __asm__ volatile("isb" ::: "memory");

// FlushCache: キャッシュの内容をメモリに書き出す (Clean)
// DMA書き込み（ホスト→デバイス）前に使用
inline void FlushCache(void *addr, size_t size)
{
    if (addr == nullptr || size == 0)
        return;

    uintptr_t start = (uintptr_t)addr & ~(64 - 1);
    uintptr_t end = ((uintptr_t)addr + size + 63) & ~(64 - 1);
    for (uintptr_t p = start; p < end; p += 64)
    {
        // dc cvac: Data Cache Clean by VA to PoC
        __asm__ volatile("dc cvac, %0" ::"r"(p) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

// InvalidateCache: キャッシュを無効化してメモリから再読み込みさせる
// DMA読み取り（デバイス→ホスト）前後に使用
// 注意: dc ivac は dirty なキャッシュラインを単に破棄するため、
// 安全のため dc civac (Clean + Invalidate) を使用
inline void InvalidateCache(void *addr, size_t size)
{
    if (addr == nullptr || size == 0)
        return;

    uintptr_t start = (uintptr_t)addr & ~(64 - 1);
    uintptr_t end = ((uintptr_t)addr + size + 63) & ~(64 - 1);
    for (uintptr_t p = start; p < end; p += 64)
    {
        // dc civac: Data Cache Clean and Invalidate by VA to PoC
        // これにより、dirty なデータを先に書き出してからインバリデートする
        __asm__ volatile("dc civac, %0" ::"r"(p) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

// FlushAndInvalidateCache: キャッシュをクリーンしてからインバリデート
// DMA双方向転送用
inline void FlushAndInvalidateCache(void *addr, size_t size)
{
    InvalidateCache(addr, size); // civac は Clean + Invalidate を行う
}

#else

#define Hlt()                                                                  \
    while (1)                                                                  \
        ;
#define CLI()                                                                  \
    while (1)                                                                  \
        ;
#define STI()                                                                  \
    while (1)                                                                  \
        ;

#endif

#if !defined(__aarch64__)
inline void FlushCache(void *, size_t) {}
inline void InvalidateCache(void *, size_t) {}
inline void FlushAndInvalidateCache(void *, size_t) {}
#endif