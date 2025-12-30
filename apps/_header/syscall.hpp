#pragma once
#include <stdint.h>

// システムコール番号 (kernel/sys/syscall.cpp と合わせる)
const uint64_t kSyscallPutChar = 1;
const uint64_t kSyscallExit = 2;
const uint64_t kSyscallListDir = 3;
const uint64_t kSyscallReadFile = 4;
const uint64_t kSyscallRead = 5;
const uint64_t kSyscallWrite = 6;
const uint64_t kSyscallYield = 10;
const uint64_t kSyscallTaskExit = 11;
const uint64_t kSyscallSpawn = 20;
const uint64_t kSyscallOpen = 21;
const uint64_t kSyscallClose = 22;
const uint64_t kSyscallDeleteFile = 23;

// システムコール発行 (引数0個)
inline uint64_t Syscall0(uint64_t syscall_number)
{
    uint64_t ret;
#if defined(__x86_64__)
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(syscall_number)
                     : "rcx", "r11", "rdi", "rsi", "rdx", "r8", "r9", "r10",
                       "memory");
#elif defined(__aarch64__)
    register uint64_t x8 asm("x8") = syscall_number;
    register uint64_t x0 asm("x0");
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory", "cc");
    ret = x0;
#endif
    return ret;
}

// システムコール発行 (引数1個)
inline uint64_t Syscall1(uint64_t syscall_number, uint64_t arg1)
{
    uint64_t ret;
#if defined(__x86_64__)
    __asm__ volatile("syscall"
                     : "=a"(ret), "+D"(arg1)
                     : "a"(syscall_number)
                     : "rcx", "r11", "rsi", "rdx", "r8", "r9", "r10", "memory");
#elif defined(__aarch64__)
    register uint64_t x8 asm("x8") = syscall_number;
    register uint64_t x0 asm("x0") = arg1;
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8), "0"(x0) : "memory", "cc");
    ret = x0;
#endif
    return ret;
}

// システムコール発行 (引数3個)
inline uint64_t Syscall3(uint64_t syscall_number, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3)
{
    uint64_t ret;
#if defined(__x86_64__)
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(syscall_number), "D"(arg1), "S"(arg2), "d"(arg3)
                     : "rcx", "r11", "r8", "r9", "r10", "memory");
#elif defined(__aarch64__)
    register uint64_t x8 asm("x8") = syscall_number;
    register uint64_t x0 asm("x0") = arg1;
    register uint64_t x1 asm("x1") = arg2;
    register uint64_t x2 asm("x2") = arg3;
    __asm__ volatile("svc #0"
                     : "=r"(x0)
                     : "r"(x8), "0"(x0), "r"(x1), "r"(x2)
                     : "memory", "cc");
    ret = x0;
#endif
    return ret;
}

// ラッパー関数
inline void PutChar(char c)
{
    Syscall1(kSyscallPutChar, c);
}

inline void Exit()
{
    Syscall0(kSyscallExit);
}

inline int Read(int fd, void *buf, int len)
{
    return (int)Syscall3(kSyscallRead, fd, (uint64_t)buf, len);
}

inline int Write(int fd, const void *buf, int len)
{
    return (int)Syscall3(kSyscallWrite, fd, (uint64_t)buf, len);
}

// 文字列を標準出力に書き込む
inline void Print(const char *s)
{
    int len = 0;
    while (s[len])
        len++;
    Write(1, s, len);
}

// ディレクトリ一覧を表示 (cluster=0でルート)
inline void ListDirectory(uint32_t cluster = 0)
{
    Syscall1(kSyscallListDir, cluster);
}

// ファイルを読み込む
inline int ReadFile(const char *path, void *buf, int len)
{
    return (int)Syscall3(kSyscallReadFile, (uint64_t)path, (uint64_t)buf, len);
}

// プロセスを起動する (戻り値: タスクID, 0で失敗)
inline uint64_t Spawn(const char *path, int argc, char **argv)
{
    return Syscall3(kSyscallSpawn, (uint64_t)path, argc, (uint64_t)argv);
}

// ファイルをオープンする (戻り値: fd, -1で失敗)
inline int Open(const char *path, int flags = 0)
{
    return (int)Syscall3(kSyscallOpen, (uint64_t)path, flags, 0);
}

// ファイルをクローズする
inline int Close(int fd)
{
    return (int)Syscall1(kSyscallClose, fd);
}

// ファイルを削除する
inline int DeleteFile(const char *path)
{
    return (int)Syscall1(kSyscallDeleteFile, (uint64_t)path);
}

// CPUを自発的に手放す
inline void Yield()
{
    Syscall0(kSyscallYield);
}