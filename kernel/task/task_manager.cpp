#include "task_manager.hpp"
#include "../cxx.hpp"
#include "../memory/memory_manager.hpp"
#include "../paging.hpp"
#include "../printk.hpp"

// 静的メンバ変数の定義
Task *TaskManager::current_task_ = nullptr;
Task *TaskManager::ready_queue_head_ = nullptr;
Task *TaskManager::ready_queue_tail_ = nullptr;
uint64_t TaskManager::next_task_id_ = 0;
uint64_t TaskManager::task_count_ = 0;

// カーネルスタックサイズ（16KB）
static const uint64_t kKernelStackSize = 16 * 1024;

void TaskManager::Initialize()
{
    current_task_ = nullptr;
    ready_queue_head_ = nullptr;
    ready_queue_tail_ = nullptr;
    next_task_id_ = 0;
    task_count_ = 0;

    kprintf("[TaskManager] Initialized.\n");
}

Task *TaskManager::CreateTask(uint64_t entry_point)
{
    // タスク構造体を割り当て
    Task *task = static_cast<Task *>(MemoryManager::Allocate(sizeof(Task)));
    if (!task)
    {
        kprintf("[TaskManager] Failed to allocate Task struct.\n");
        return nullptr;
    }
    memset(task, 0, sizeof(Task));

    // カーネルスタックを割り当て
    void *stack = MemoryManager::Allocate(kKernelStackSize);
    if (!stack)
    {
        kprintf("[TaskManager] Failed to allocate kernel stack.\n");
        MemoryManager::Free(task, sizeof(Task));
        return nullptr;
    }
    memset(stack, 0, kKernelStackSize);

    // タスクの基本情報を設定
    task->task_id = next_task_id_++;
    task->state = TaskState::READY;
    task->kernel_stack = stack;
    task->kernel_stack_size = kKernelStackSize;
    task->next = nullptr;
    task->prev = nullptr;

    // コンテキストを初期化
    memset(&task->context, 0, sizeof(TaskContext));

    // スタックポインタ（スタックは高位から低位へ伸びる）
    uint64_t stack_top = reinterpret_cast<uint64_t>(stack) + kKernelStackSize;
    // 16バイトアライメントを確保
    stack_top = stack_top & ~0xFULL;

    // スタックにエントリーポイントを積む（SwitchContextのretで使用）
    // x86-64のABI: 関数呼び出し時はスタックが16バイト境界 - 8であるべき
    stack_top -= 8;
    *reinterpret_cast<uint64_t *>(stack_top) = entry_point;

    task->context.rsp = stack_top;

    // エントリーポイント（コンテキストにも保存しておく）
    task->context.rip = entry_point;

    // RFLAGS: 割り込み有効（IF=1）
    task->context.rflags = 0x202; // IF=1, Reserved bit 1=1

    // セグメントレジスタ（カーネルモード）
    task->context.cs = 0x08; // Kernel CS
    task->context.ss = 0x10; // Kernel DS
    task->context.ds = 0x10;
    task->context.es = 0x10;
    task->context.fs = 0x00;
    task->context.gs = 0x00;

    // CR3: 現在のページテーブルを共有（フェーズ1）
    task->context.cr3 = GetCR3();

    task_count_++;

    kprintf("[TaskManager] Created Task ID=%lu, Entry=%lx\n", task->task_id,
            entry_point);

    return task;
}

void TaskManager::TerminateTask(Task *task)
{
    if (!task)
        return;

    // キューから削除
    RemoveFromReadyQueue(task);

    // 状態を終了済みに変更
    task->state = TaskState::TERMINATED;

    // スタックを解放
    if (task->kernel_stack)
    {
        MemoryManager::Free(task->kernel_stack, task->kernel_stack_size);
    }

    // タスク構造体を解放
    MemoryManager::Free(task, sizeof(Task));

    task_count_--;

    kprintf("[TaskManager] Terminated Task.\n");
}

Task *TaskManager::GetCurrentTask()
{
    return current_task_;
}

void TaskManager::SetCurrentTask(Task *task)
{
    current_task_ = task;
}

Task *TaskManager::GetNextTask()
{
    if (!ready_queue_head_)
    {
        return nullptr;
    }

    // ラウンドロビン: キューの先頭を取得
    Task *next = ready_queue_head_;
    return next;
}

void TaskManager::AddToReadyQueue(Task *task)
{
    if (!task)
        return;

    // 既にキューにある場合は追加しない
    if (task->next || task->prev || task == ready_queue_head_)
    {
        return;
    }

    task->state = TaskState::READY;
    task->next = nullptr;
    task->prev = ready_queue_tail_;

    if (ready_queue_tail_)
    {
        ready_queue_tail_->next = task;
    }
    else
    {
        ready_queue_head_ = task;
    }
    ready_queue_tail_ = task;
}

void TaskManager::RemoveFromReadyQueue(Task *task)
{
    if (!task)
        return;

    // 先頭の場合
    if (task == ready_queue_head_)
    {
        ready_queue_head_ = task->next;
    }

    // 末尾の場合
    if (task == ready_queue_tail_)
    {
        ready_queue_tail_ = task->prev;
    }

    // 前後をつなげる
    if (task->prev)
    {
        task->prev->next = task->next;
    }
    if (task->next)
    {
        task->next->prev = task->prev;
    }

    task->next = nullptr;
    task->prev = nullptr;
}

void TaskManager::BlockTask(Task *task)
{
    if (!task)
        return;

    RemoveFromReadyQueue(task);
    task->state = TaskState::BLOCKED;
}

void TaskManager::WakeTask(Task *task)
{
    if (!task)
        return;

    if (task->state == TaskState::BLOCKED)
    {
        AddToReadyQueue(task);
    }
}

uint64_t TaskManager::GetTaskCount()
{
    return task_count_;
}
