#include "test_task.hpp"
#include "../printk.hpp"
#include "scheduler.hpp"
#include "task_manager.hpp"


// 各テストタスクのカウンタ（切り替え回数を記録）
static volatile uint64_t g_task_a_counter = 0;
static volatile uint64_t g_task_b_counter = 0;
static volatile uint64_t g_task_c_counter = 0;

// テストタスクA: 定期的にメッセージを出力
void TestTaskA()
{
    kprintf("[TaskA] Started! ID=%lu\n",
            TaskManager::GetCurrentTask()->task_id);

    while (1)
    {
        g_task_a_counter++;

        // 100回ごとにメッセージを出力（出力頻度を抑える）
        if (g_task_a_counter % 100 == 0)
        {
            kprintf("[A:%lu] ", g_task_a_counter);
        }

        // 少し待機してからYield
        for (volatile int i = 0; i < 100000; i++)
        {
            // ビジーウェイト
        }

        // 次のタスクへ切り替え（協調的マルチタスク）
        Scheduler::Yield();
    }
}

// テストタスクB: 定期的にメッセージを出力
void TestTaskB()
{
    kprintf("[TaskB] Started! ID=%lu\n",
            TaskManager::GetCurrentTask()->task_id);

    while (1)
    {
        g_task_b_counter++;

        if (g_task_b_counter % 100 == 0)
        {
            kprintf("[B:%lu] ", g_task_b_counter);
        }

        for (volatile int i = 0; i < 100000; i++)
        {
            // ビジーウェイト
        }

        Scheduler::Yield();
    }
}

// テストタスクC: 定期的にメッセージを出力
void TestTaskC()
{
    kprintf("[TaskC] Started! ID=%lu\n",
            TaskManager::GetCurrentTask()->task_id);

    while (1)
    {
        g_task_c_counter++;

        if (g_task_c_counter % 100 == 0)
        {
            kprintf("[C:%lu] ", g_task_c_counter);
        }

        for (volatile int i = 0; i < 100000; i++)
        {
            // ビジーウェイト
        }

        Scheduler::Yield();
    }
}

void InitializeTestTasks()
{
    kprintf("[TestTask] Creating test tasks...\n");

    // テストタスクAを作成
    Task *taskA =
        TaskManager::CreateTask(reinterpret_cast<uint64_t>(TestTaskA));
    if (taskA)
    {
        TaskManager::AddToReadyQueue(taskA);
        kprintf("[TestTask] TaskA created (ID=%lu)\n", taskA->task_id);
    }

    // テストタスクBを作成
    Task *taskB =
        TaskManager::CreateTask(reinterpret_cast<uint64_t>(TestTaskB));
    if (taskB)
    {
        TaskManager::AddToReadyQueue(taskB);
        kprintf("[TestTask] TaskB created (ID=%lu)\n", taskB->task_id);
    }

    // テストタスクCを作成
    Task *taskC =
        TaskManager::CreateTask(reinterpret_cast<uint64_t>(TestTaskC));
    if (taskC)
    {
        TaskManager::AddToReadyQueue(taskC);
        kprintf("[TestTask] TaskC created (ID=%lu)\n", taskC->task_id);
    }

    kprintf("[TestTask] All test tasks created. Total tasks: %lu\n",
            TaskManager::GetTaskCount());
}
