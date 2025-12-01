#pragma once
#include <stdint.h>

// 最大コマンド長
const int kMaxCommandLen = 100;

class Shell
{
public:
    Shell();

    void OnKey(char c);

private:
    char buffer_[kMaxCommandLen];
    int cursor_pos_;
    uint32_t current_cluster_;

    void PrintPrompt();
    void ExecuteCommand();
};

extern Shell *g_shell;