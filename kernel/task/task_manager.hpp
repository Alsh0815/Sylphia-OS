#pragma once
#include "task.hpp"

class TaskManager
{
  public:
    // 初期化
    static void Initialize();

    // タスク作成（フェーズ1: 同じページテーブルを共有）
    // entry_point: タスクのエントリーポイント関数のアドレス
    // 戻り値: 作成されたタスクへのポインタ
    static Task *CreateTask(uint64_t entry_point);

    // タスク終了
    static void TerminateTask(Task *task);

    // 現在のタスクを取得
    static Task *GetCurrentTask();

    // 現在のタスクを設定
    static void SetCurrentTask(Task *task);

    // 次に実行するタスクを取得（スケジューラから呼ばれる）
    static Task *GetNextTask();

    // タスクをレディキューに追加
    static void AddToReadyQueue(Task *task);

    // タスクをレディキューから削除
    static void RemoveFromReadyQueue(Task *task);

    // タスクをブロック状態に移行
    static void BlockTask(Task *task);

    // タスクをウェイクアップ
    static void WakeTask(Task *task);

    // タスク数を取得
    static uint64_t GetTaskCount();

  private:
    static Task *current_task_;     // 現在実行中のタスク
    static Task *ready_queue_head_; // レディキューの先頭
    static Task *ready_queue_tail_; // レディキューの末尾
    static uint64_t next_task_id_;  // 次に割り当てるタスクID
    static uint64_t task_count_;    // 管理しているタスク数
};

// グローバルアクセス用（外部から参照される場合）
extern TaskManager *g_task_manager;
