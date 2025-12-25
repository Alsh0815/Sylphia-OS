#pragma once
#include <stdint.h>

// タスク状態
enum class TaskState
{
    READY,     // 実行可能状態
    RUNNING,   // 現在実行中
    BLOCKED,   // ブロック状態（I/O待ちなど）
    TERMINATED // 終了済み
};

// タスクコンテキスト（レジスタの保存領域）
// context_switch.asm と同じオフセットで構成
struct TaskContext
{
    // 汎用レジスタ（オフセット0-120）
    uint64_t r15; // 0
    uint64_t r14; // 8
    uint64_t r13; // 16
    uint64_t r12; // 24
    uint64_t r11; // 32
    uint64_t r10; // 40
    uint64_t r9;  // 48
    uint64_t r8;  // 56
    uint64_t rdi; // 64
    uint64_t rsi; // 72
    uint64_t rbp; // 80
    uint64_t rsp; // 88
    uint64_t rbx; // 96
    uint64_t rdx; // 104
    uint64_t rcx; // 112
    uint64_t rax; // 120

    // セグメントレジスタ（オフセット128-168）
    uint64_t cs; // 128
    uint64_t ss; // 136
    uint64_t ds; // 144
    uint64_t es; // 152
    uint64_t fs; // 160
    uint64_t gs; // 168

    // 制御レジスタ（オフセット176-192）
    uint64_t rip;    // 176 - 命令ポインタ
    uint64_t rflags; // 184 - ステータスフラグ
    uint64_t cr3;    // 192 - ページテーブルベースアドレス
};

// タスク制御ブロック（TCB）
struct Task
{
    uint64_t task_id;    // タスクID（ユニーク）
    TaskState state;     // タスク状態
    TaskContext context; // コンテキスト情報

    void *kernel_stack;         // カーネルスタックの先頭アドレス
    uint64_t kernel_stack_size; // カーネルスタックのサイズ

    Task *next; // 次のタスク（リンクリスト用）
    Task *prev; // 前のタスク（リンクリスト用）

    // アプリ用フィールド
    char name[32];        // タスク名
    uint64_t entry_point; // エントリーポイント（アプリ用）
    int argc;             // 引数の数
    char **argv;          // 引数配列
    bool is_app;          // アプリタスクかどうか

    // Ring 3プロセス用フィールド
    void *user_stack;         // ユーザースタックの仮想アドレス
    uint64_t user_stack_size; // ユーザースタックのサイズ
    uint64_t user_stack_top;  // ユーザースタックのトップ（SPの初期値）

    // プロセスごとのファイルディスクリプタテーブル
    // nullptr の場合はグローバルな g_fds を使用（後方互換性）
    // 0: stdin, 1: stdout, 2: stderr, 3-15: その他
    static const int kMaxFDs = 16;
    void *process_fds[kMaxFDs]; // FileDescriptor*
                                // の配列（循環参照回避のためvoid*）

    // 親プロセスのタスクID（0 = カーネルから直接起動）
    uint64_t parent_task_id;
};
