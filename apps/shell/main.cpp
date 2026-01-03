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

// 簡易atoi
int Atoi(const char *s)
{
    int result = 0;
    int sign = 1;
    if (*s == '-')
    {
        sign = -1;
        s++;
    }
    while (*s >= '0' && *s <= '9')
    {
        result = result * 10 + (*s - '0');
        s++;
    }
    return result * sign;
}

// 整数を表示
void PrintInt(int n)
{
    if (n < 0)
    {
        Print("-");
        n = -n;
    }
    if (n == 0)
    {
        Print("0");
        return;
    }
    char buf[16];
    int i = 0;
    while (n > 0)
    {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    // 逆順に出力
    while (i > 0)
    {
        char s[2] = {buf[--i], 0};
        Print(s);
    }
}

// レンダーモード名を取得
const char *GetModeName(int mode)
{
    switch (mode)
    {
        case 1:
            return "STANDARD";
        case 2:
            return "DOUBLE_BUFFER";
        case 3:
            return "TRIPLE_BUFFER";
        default:
            return "UNKNOWN";
    }
}

// ディスプレイ情報を表示
void PrintDisplayInfo(const DisplayInfo &info)
{
    Print("Display ");
    PrintInt(info.id);
    Print(": ");
    PrintInt(info.width);
    Print("x");
    PrintInt(info.height);
    Print(" Mode=");
    Print(GetModeName(info.render_mode));
    Print("\n");
}

// シェルクラス
class Shell
{
private:
    static const int kMaxCommandLen = 256;
    char buffer_[kMaxCommandLen];
    int cursor_pos_;
    bool prompt_shown_; // プロンプトが表示されているか

public:
    Shell() : cursor_pos_(0), prompt_shown_(false)
    {
        memset(buffer_, 0, kMaxCommandLen);
    }

    void PrintPrompt()
    {
        Print("Sylphia:/$ ");
        prompt_shown_ = true;
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

        // プロンプトが未表示なら表示（外部コマンド実行後の復帰時）
        if (!prompt_shown_)
        {
            PrintPrompt();
        }

        if (c == '\n')
        {
            // エンターキー: コマンド実行
            Print("\n");
            ExecuteCommand();

            // バッファクリアしてプロンプト再表示
            cursor_pos_ = 0;
            memset(buffer_, 0, kMaxCommandLen);
            // 外部コマンドでない場合はプロンプトを表示
            // 外部コマンドの場合はアプリ終了後にOnKeyで表示
            if (prompt_shown_)
            {
                PrintPrompt();
            }
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
            SystemInfo info;
            if (GetSystemInfo(&info) == 0)
            {
                Print("=============== Sylphia-OS ===============\n");
                Print("Version: ");
                PrintInt(info.version_major);
                Print(".");
                PrintInt(info.version_minor);
                Print(".");
                PrintInt(info.version_patch);
                Print(".");
                PrintInt(info.version_revision);
                Print("\n");
                Print("Build: ");
                PrintInt(info.build_year);
                Print("/");
                PrintInt(info.build_month);
                Print("/");
                PrintInt(info.build_day);
                Print("\n");
                Print("==========================================\n");
            }
            else
            {
                Print("Failed to get system info.\n");
            }
        }
        else if (strcmp(argv[0], "display") == 0)
        {
            if (argc == 1)
            {
                // ディスプレイ一覧表示
                DisplayInfo info[8];
                int count = GetDisplayInfo(info, 8);
                if (count == 0)
                {
                    Print("No displays found.\n");
                }
                else
                {
                    Print("=== Display Info ===\n");
                    for (int i = 0; i < count; ++i)
                    {
                        PrintDisplayInfo(info[i]);
                    }
                }
            }
            else if (argc >= 5 && strcmp(argv[1], "select") == 0 &&
                     strcmp(argv[3], "mode") == 0)
            {
                // display select <id> mode <mode>
                int id = Atoi(argv[2]);
                int mode = Atoi(argv[4]);
                if (mode < 1 || mode > 3)
                {
                    Print("Invalid mode. Use 1=STANDARD, 2=DOUBLE, 3=TRIPLE\n");
                }
                else
                {
                    int ret = SetDisplayMode(id, mode);
                    if (ret == 0)
                    {
                        Print("Display ");
                        PrintInt(id);
                        Print(" mode set to ");
                        Print(GetModeName(mode));
                        Print("\n");
                    }
                    else
                    {
                        Print("Failed to set display mode.\n");
                    }
                }
            }
            else
            {
                Print("Usage:\n");
                Print(
                    "  display                          - Show all displays\n");
                Print("  display select <id> mode <mode>  - Set render mode\n");
                Print("    mode: 1=STANDARD, 2=DOUBLE, 3=TRIPLE\n");
            }
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
            else
            {
                // アプリが起動したので、プロンプトはアプリ終了後に表示
                prompt_shown_ = false;
            }
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
