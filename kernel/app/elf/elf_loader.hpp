#pragma once
#include <stdint.h>

// アプリ実行状態を追跡
extern bool g_app_running;

class ElfLoader
{
  public:
    static bool LoadAndRun(const char *filename, int argc, char **argv);
};