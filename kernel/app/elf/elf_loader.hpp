#pragma once
#include "task/task.hpp"
#include <stdint.h>

// アプリ実行状態を追跡（レガシー互換用）
extern bool g_app_running;

class ElfLoader
{
  public:
    // レガシーAPI（同期的実行）
    static bool LoadAndRun(const char *filename, int argc, char **argv);

    // 新API: ELFをロードしてタスクを作成（非同期）
    // 戻り値: 作成されたタスク（失敗時はnullptr）
    static Task *CreateProcess(const char *filename, int argc, char **argv);

    // ELFファイルをメモリにロード（内部関数）
    // entry_point_out: エントリーポイントを返す
    // 戻り値: 成功したかどうか
    static bool LoadElf(const char *filename, uint64_t *entry_point_out);
};