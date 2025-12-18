#pragma once
#include "task.hpp"

// アイドルタスクのエントリーポイント
void IdleTaskEntry();

// アイドルタスクを初期化
void InitializeIdleTask();

// アイドルタスクへのポインタ（デバッグ用）
extern Task *g_idle_task;
