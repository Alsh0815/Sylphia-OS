#include "shell/shell.hpp"
#include "console.hpp"
#include "printk.hpp"

Shell *g_shell = nullptr;

// 簡易strcmp (一致したら0を返す)
int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

// 簡易memset
void memset(void *dest, int val, int len)
{
    unsigned char *ptr = (unsigned char *)dest;
    while (len-- > 0)
        *ptr++ = val;
}

Shell::Shell() : cursor_pos_(0)
{
    memset(buffer_, 0, kMaxCommandLen);
}

void Shell::PrintPrompt()
{
    kprintf("Sylphia> ");
}

void Shell::OnKey(char c)
{
    if (c == 0)
        return;

    if (c == '\n')
    {
        // エンターキー: コマンド実行
        kprintf("\n"); // 改行して
        ExecuteCommand();

        // バッファクリアしてプロンプト再表示
        cursor_pos_ = 0;
        memset(buffer_, 0, kMaxCommandLen);
        PrintPrompt();
    }
    else if (c == '\b')
    {
        // バックスペース
        if (cursor_pos_ > 0)
        {
            cursor_pos_--;
            buffer_[cursor_pos_] = 0; // バッファから消す
            kprintf("\b");            // 画面からも消す (Console側で処理される)
        }
    }
    else
    {
        // 通常文字: バッファがいっぱいでなければ追加
        if (cursor_pos_ < kMaxCommandLen - 1)
        {
            buffer_[cursor_pos_++] = c;
            // 画面にエコーバック
            char s[2] = {c, 0};
            kprintf("%s", s);
        }
    }
}

void Shell::ExecuteCommand()
{
    if (cursor_pos_ == 0)
        return; // 空なら何もしない

    // コマンド分岐
    if (strcmp(buffer_, "hello") == 0)
    {
        kprintf("Hello! I am Sylphia-OS.\n");
    }
    else if (strcmp(buffer_, "help") == 0)
    {
        kprintf("Available commands: hello, help, clear, whoami\n");
    }
    else if (strcmp(buffer_, "clear") == 0)
    {
        // 画面クリア機能 (未実装なら改行連打でごまかす)
        // ※GraphicsやConsoleにClearメソッドを追加するのが理想
        for (int i = 0; i < 30; i++)
            kprintf("\n");
        // カーソル位置リセット等の機能も必要ですが、簡易的に
    }
    else if (strcmp(buffer_, "whoami") == 0)
    {
        kprintf("root\n");
    }
    else
    {
        kprintf("Unknown command: %s\n", buffer_);
    }
}