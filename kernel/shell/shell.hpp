#pragma once
#include <stdint.h>

// 最大コマンド長
const int kMaxCommandLen = 100;

class Shell
{
public:
    Shell();
    // キー入力があったときに呼ばれる関数
    void OnKey(char c);

private:
    // 入力バッファ
    char buffer_[kMaxCommandLen];
    // 現在のカーソル位置（バッファ内のインデックス）
    int cursor_pos_;

    // プロンプト表示 (例: "Sylphia> ")
    void PrintPrompt();
    // コマンド実行
    void ExecuteCommand();
};

// グローバルなシェルインスタンス
extern Shell *g_shell;