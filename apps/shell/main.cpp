#include "../_header/syscall.hpp"

// 簡易memset
void memset(void *dest, int val, int len)
{
    unsigned char *ptr = (unsigned char *)dest;
    while (len-- > 0)
        *ptr++ = val;
}

// 簡易strcmp
int strcmp(const char *s1, const char *s2)
{
    while (*s1 && *s1 == *s2)
    {
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

// 簡易strlen
int strlen(const char *s)
{
    int len = 0;
    while (*s++)
        len++;
    return len;
}

// 簡易strcpy
char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++))
        ;
    return dest;
}

// 簡易strcat
char *strcat(char *dest, const char *src)
{
    char *d = dest;
    while (*d)
        d++;
    while ((*d++ = *src++))
        ;
    return dest;
}

// シェルクラス
class Shell
{
  private:
    static const int kMaxCommandLen = 256;
    char buffer_[kMaxCommandLen];
    int cursor_pos_;

  public:
    Shell() : cursor_pos_(0)
    {
        memset(buffer_, 0, kMaxCommandLen);
    }

    void PrintPrompt()
    {
        Print("Sylphia:/$ ");
    }

    void Run()
    {
        Print("\nWelcome to Sylphia-OS Shell!\n");
        PrintPrompt();

        while (true)
        {
            char buf[16];
            int len = Read(0, buf, sizeof(buf));

            for (int i = 0; i < len; ++i)
            {
                OnKey(buf[i]);
            }

            // 少し待機してCPUを手放す
            Yield();
        }
    }

    void OnKey(char c)
    {
        if (c == 0)
            return;

        if (c == '\n')
        {
            // エンターキー: コマンド実行
            Print("\n");
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
                buffer_[cursor_pos_] = 0;
                Print("\b");
            }
        }
        else
        {
            // 通常文字
            if (cursor_pos_ < kMaxCommandLen - 1)
            {
                buffer_[cursor_pos_++] = c;
                char s[2] = {c, 0};
                Print(s);
            }
        }
    }

    void ExecuteCommand()
    {
        if (cursor_pos_ == 0)
            return;

        char *argv[32];
        int argc = 0;

        // コマンドラインをパース
        char *p = buffer_;
        while (*p)
        {
            while (*p == ' ')
                *p++ = 0;
            if (*p == 0)
                break;
            argv[argc++] = p;
            if (argc >= 32)
                break;
            while (*p && *p != ' ')
                p++;
        }
        if (argc == 0)
            return;

        // 内蔵コマンド
        if (strcmp(argv[0], "clear") == 0)
        {
            for (int i = 0; i < 30; i++)
                Print("\n");
        }
        else if (strcmp(argv[0], "echo") == 0)
        {
            if (argc > 1)
            {
                Print(argv[1]);
                Print("\n");
            }
        }
        else if (strcmp(argv[0], "ls") == 0)
        {
            ListDirectory(0);
        }
        else if (strcmp(argv[0], "cat") == 0)
        {
            if (argc > 1)
            {
                char buf[4096];
                memset(buf, 0, sizeof(buf));
                int bytes = ReadFile(argv[1], buf, sizeof(buf) - 1);
                if (bytes > 0)
                {
                    buf[bytes] = 0;
                    Print(buf);
                    Print("\n");
                }
                else
                {
                    Print("Error: File not found or empty.\n");
                }
            }
            else
            {
                Print("Usage: cat <filename>\n");
            }
        }
        else if (strcmp(argv[0], "rm") == 0)
        {
            if (argc > 1)
            {
                if (DeleteFile(argv[1]) == 0)
                {
                    Print("Deleted ");
                    Print(argv[1]);
                    Print("\n");
                }
                else
                {
                    Print("Could not delete ");
                    Print(argv[1]);
                    Print("\n");
                }
            }
            else
            {
                Print("Usage: rm <filename>\n");
            }
        }
        else if (strcmp(argv[0], "sys") == 0)
        {
            Print("=============== Sylphia-OS ZERO ===============\n");
            Print("Shell running in user mode!\n");
            Print("===============================================\n");
        }
        else if (strcmp(argv[0], "exit") == 0)
        {
            Print("Exiting shell...\n");
            Exit();
        }
        else
        {
            // 外部コマンド実行
            char path[64];
            strcpy(path, "/sys/bin/");
            strcat(path, argv[0]);

            uint64_t task_id = Spawn(path, argc, argv);
            if (task_id == 0)
            {
                Print("Unknown command: ");
                Print(argv[0]);
                Print("\n");
            }
            // Spawnはすぐに戻るので、プロセスの終了は待たない
        }
    }
};

// グローバルシェルインスタンス
Shell g_shell;

extern "C" int main(int argc, char **argv)
{
    g_shell.Run();
    return 0;
}
