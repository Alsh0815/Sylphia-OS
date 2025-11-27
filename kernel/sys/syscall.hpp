#pragma once
#include <stdint.h>

// システムコール呼び出し時のコンテキスト保存用
// KERNEL_GS_BASE MSR にこの構造体のアドレスを登録しておく
struct SyscallContext
{
    uint64_t kernel_stack_ptr; // カーネル用スタックポインタ (初期化時に設定)
    uint64_t user_stack_ptr;   // ユーザー用スタックポインタ (syscall入り口で保存)
};

// 初期化関数
void InitializeSyscall();