#include "idle_task.hpp"
#include "../printk.hpp"
#include "task_manager.hpp"

Task *g_idle_task = nullptr;

void IdleTaskEntry()
{
    kprintf("[IdleTask] *** TASK STARTED! ***\n");
    // アイドルタスク: hltで待機し、タイマー割り込みで起こされる
    while (1)
    {
        __asm__ volatile("hlt"); // CPUを停止して割り込み待ち
    }
}

void InitializeIdleTask()
{
    // アイドルタスクを作成
    g_idle_task =
        TaskManager::CreateTask(reinterpret_cast<uint64_t>(IdleTaskEntry));

    if (g_idle_task)
    {
        TaskManager::AddToReadyQueue(g_idle_task);
        kprintf("[IdleTask] Created and added to ready queue.\n");
    }
    else
    {
        kprintf("[IdleTask] Failed to create idle task!\n");
    }
}
