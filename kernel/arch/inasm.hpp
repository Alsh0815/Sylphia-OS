#pragma once

#if defined(__x86_64__)

#define Hlt() __asm__ volatile("hlt")
#define CLI() __asm__ volatile("cli")
#define STI() __asm__ volatile("sti")
#define PAUSE() __asm__ volatile("pause");
#define WBINVD() __asm__ volatile("wbinvd");

#elif defined(__aarch64__)

#define Hlt() __asm__ volatile("wfi")
#define CLI() __asm__ volatile("msr daifset, #2")
#define STI() __asm__ volatile("msr daifclr, #2")
#define PAUSE() __asm__ volatile("yield");
#define WBINVD() __asm__ volatile("tlbi vmalle1is");

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