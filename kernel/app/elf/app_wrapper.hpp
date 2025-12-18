#pragma once
#include "task/task.hpp"

// アプリタスクのエントリーポイントラッパー
// タスクが起動されると、この関数が呼ばれ、argc/argvを使ってアプリのmainを呼び出す
void AppTaskEntry();

// アプリタスクを作成する
// filename: ELFファイル名
// argc: 引数の数
// argv: 引数配列
// 戻り値: 作成されたタスク（失敗時はnullptr）
Task *CreateAppTask(const char *filename, int argc, char **argv);
