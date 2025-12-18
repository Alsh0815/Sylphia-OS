#pragma once
#include "task.hpp"

// コンテキストスイッチ関数（アセンブリで実装）
extern "C" void SwitchContext(TaskContext *old_ctx, TaskContext *new_ctx);

class Scheduler
{
  public:
    // 初期化
    static void Initialize();

    // タイマー割り込みから呼ばれるスケジュール関数
    // 現在のタスクのコンテキストを保存し、次のタスクへ切り替え
    static void Schedule();

    // 現在のタスクを終了して次へ
    static void Yield();

    // スケジューラが有効かどうか
    static bool IsEnabled();

    // スケジューラを有効化
    static void Enable();

    // スケジューラを無効化
    static void Disable();

  private:
    static bool enabled_;            // スケジューラが有効かどうか
    static uint32_t schedule_count_; // スケジュール回数（デバッグ用）
};
