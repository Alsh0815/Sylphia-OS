#include "scheduler.hpp"
#include "../printk.hpp"
#include "task_manager.hpp"

// 静的メンバ変数の定義
bool Scheduler::enabled_ = false;
uint32_t Scheduler::schedule_count_ = 0;

void Scheduler::Initialize()
{
    enabled_ = false;
    schedule_count_ = 0;
    kprintf("[Scheduler] Initialized.\n");
}

void Scheduler::Schedule()
{
    // スケジューラが無効なら何もしない
    if (!enabled_)
    {
        return;
    }

    Task *current = TaskManager::GetCurrentTask();
    Task *next = TaskManager::GetNextTask();

    // デバッグ: 最初の10回だけ出力
    /*
    if (schedule_count_ < 10)
    {
        kprintf("[Sched] cur=%lx next=%lx\n", (uint64_t)current,
                (uint64_t)next);
    }
    */

    // 次のタスクがなければ何もしない
    if (!next)
    {
        return;
    }

    // 現在のタスクと次のタスクが同じなら何もしない
    if (current == next)
    {
        return;
    }

    // 現在のタスクをレディキューの末尾に移動
    if (current && current->state == TaskState::RUNNING)
    {
        TaskManager::RemoveFromReadyQueue(current);
        current->state = TaskState::READY;
        TaskManager::AddToReadyQueue(current);
    }

    // 次のタスクをキューから削除して実行状態に
    TaskManager::RemoveFromReadyQueue(next);
    next->state = TaskState::RUNNING;
    TaskManager::SetCurrentTask(next);

    schedule_count_++;

    // デバッグ: コンテキストスイッチ前
    if (schedule_count_ <= 10)
    {
        kprintf("[Sched] Switching to Task ID=%lu\n", next->task_id);
    }

    // コンテキストスイッチを実行
    if (current)
    {
        SwitchContext(&current->context, &next->context);
    }
    else
    {
        // 最初のタスク起動時（currentがない場合）
        // ダミーのコンテキストを使用
        static TaskContext dummy_context;
        SwitchContext(&dummy_context, &next->context);
    }
}

void Scheduler::Yield()
{
    // 割り込みを禁止してスケジュール
    __asm__ volatile("cli");
    Schedule();
    __asm__ volatile("sti");
}

bool Scheduler::IsEnabled()
{
    return enabled_;
}

void Scheduler::Enable()
{
    enabled_ = true;
    kprintf("[Scheduler] Enabled.\n");
}

void Scheduler::Disable()
{
    enabled_ = false;
    kprintf("[Scheduler] Disabled.\n");
}
