#pragma once
#include <stdint.h>

// システムコール番号 (kernel/sys/syscall.cpp と合わせる)
const uint64_t kSyscallPutChar = 1;
const uint64_t kSyscallExit = 2;
// const uint64_t kSyscallLs      = 3;
// const uint64_t kSyscallRead    = 4;

// システムコール発行 (引数0個)
inline uint64_t Syscall0(uint64_t syscall_number) {
  uint64_t ret;
  __asm__ volatile("syscall"
                   : "=a"(ret)
                   : "a"(syscall_number)
                   : "rcx", "r11", "rdi", "rsi", "rdx", "r8", "r9", "r10",
                     "memory");
  return ret;
}

// システムコール発行 (引数1個)
inline uint64_t Syscall1(uint64_t syscall_number, uint64_t arg1) {
  uint64_t ret;
  __asm__ volatile("syscall"
                   : "=a"(ret), "+D"(arg1)
                   : "a"(syscall_number)
                   : "rcx", "r11", "rsi", "rdx", "r8", "r9", "r10", "memory");
  return ret;
}

// ラッパー関数
inline void PutChar(char c) { Syscall1(kSyscallPutChar, c); }

inline void Exit() { Syscall0(kSyscallExit); }