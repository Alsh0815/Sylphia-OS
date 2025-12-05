#include "shell/shell.hpp"
#include "app/elf/elf_loader.hpp"
#include "console.hpp"
#include "driver/usb/keyboard/keyboard.hpp"
#include "fs/fat32/fat32_driver.hpp"
#include "memory/memory_manager.hpp"
#include "printk.hpp"
#include "sys/logger/logger.hpp"
#include "sys/std/file_descriptor.hpp"
#include "sys/sys.hpp"
#include <std/string.hpp>

Shell *g_shell = nullptr;

// 簡易memset
void memset(void *dest, int val, int len)
{
    unsigned char *ptr = (unsigned char *)dest;
    while (len-- > 0)
        *ptr++ = val;
}

Shell::Shell() : cursor_pos_(0), current_cluster_(0)
{
    memset(buffer_, 0, kMaxCommandLen);
}

void Shell::PrintPrompt()
{
    kprintf("Sylphia:/$ ");
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
        return;

    // パイプ処理 (|)
    char *pipe_pos = nullptr;
    for (int i = 0; i < cursor_pos_; ++i)
    {
        if (buffer_[i] == '|')
        {
            buffer_[i] = 0;
            pipe_pos = &buffer_[i + 1];
            break;
        }
    }

    if (pipe_pos)
    {
        // パイプあり: コマンドA | コマンドB
        PipeFD *pipe = new PipeFD();

        // Stdout(1) をパイプに退避/差し替え
        FileDescriptor *original_stdout = g_fds[1];
        g_fds[1] = pipe;

        ExecuteSingleCommand(buffer_);

        g_fds[1] = original_stdout;

        // Stdin(0) をパイプに退避/差し替え
        FileDescriptor *original_stdin = g_fds[0];
        g_fds[0] = pipe;

        ExecuteSingleCommand(pipe_pos);

        g_fds[0] = original_stdin;

        delete pipe;
    }
    else
    {
        ExecuteSingleCommand(buffer_);
    }
}

void Shell::ExecuteSingleCommand(char *cmd_line)
{
    char *argv[32]; // Max 32 arguments
    int argc = 0;

    char *p = cmd_line;
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

    if (strcmp(argv[0], "cat") == 0)
    {
        if (argc == 1)
        {
            // 引数なしcat: Stdin -> Stdout
            char buf[128];
            while (true)
            {
                int len = g_fds[0]->Read(buf, sizeof(buf) - 1);
                if (len <= 0)
                    break;
                g_fds[1]->Write(buf, len);
                if (g_fds[0]->GetType() == FD_KEYBOARD)
                    break;
            }
        }
        else
        {
            char *filename = argv[1];
            if (!FileSystem::g_fat32_driver)
            {
                kprintf("Error: File System not initialized.\n");
                return;
            }
            uint32_t buf_size = 4096;
            char *buf = static_cast<char *>(MemoryManager::Allocate(buf_size));
            memset(buf, 0, buf_size);
            uint32_t bytes_read =
                FileSystem::g_fat32_driver->ReadFile(filename, buf, buf_size);
            if (bytes_read > 0)
            {
                g_fds[1]->Write(buf, bytes_read);
                char nl = '\n';
                g_fds[1]->Write(&nl, 1);
            }
            else
            {
                kprintf("Error: File not found or empty.\n");
            }
            MemoryManager::Free(buf, buf_size);
        }
    }
    else if (strcmp(argv[0], "clear") == 0)
    {
        for (int i = 0; i < 30; i++)
            kprintf("\n");
    }
    else if (strcmp(argv[0], "echo") == 0)
    {
        if (argc > 1)
        {
            g_fds[1]->Write(argv[1], strlen(argv[1]));
            char nl = '\n';
            g_fds[1]->Write(&nl, 1);
        }
    }
    else if (strcmp(argv[0], "ls") == 0)
    {
        if (FileSystem::g_fat32_driver)
        {
            FileSystem::g_fat32_driver->ListDirectory();
        }
        else
        {
            kprintf("Error: File System not initialized.\n");
        }
    }
    else if (strcmp(argv[0], "rm") == 0)
    {
        char *filename = argv[1];
        if (FileSystem::g_fat32_driver)
        {
            if (FileSystem::g_fat32_driver->DeleteFile(filename))
            {
                kprintf("Deleted %s\n", filename);
            }
            else
            {
                kprintf("Could not delete %s\n", filename);
            }
        }
        else
        {
            kprintf("File System not initialized.\n");
        }
    }
    else if (strcmp(argv[0], "logger") == 0)
    {
        // ロガーが初期化されていない場合
        if (!Sys::Logger::g_event_logger)
        {
            kprintf("Error: Logger not initialized.\n");
            return;
        }

        // オプション解析
        Sys::Logger::LogLevel *filter_level = nullptr;
        Sys::Logger::LogType *filter_type = nullptr;
        const char *keyword = nullptr;
        bool do_flush = false;

        Sys::Logger::LogLevel level_val;
        Sys::Logger::LogType type_val;

        for (int i = 1; i < argc; i++)
        {
            if (strcmp(argv[i], "-l") == 0 && i + 1 < argc)
            {
                // レベルフィルタ
                i++;
                if (strcmp(argv[i], "info") == 0)
                {
                    level_val = Sys::Logger::LogLevel::Info;
                    filter_level = &level_val;
                }
                else if (strcmp(argv[i], "warn") == 0)
                {
                    level_val = Sys::Logger::LogLevel::Warning;
                    filter_level = &level_val;
                }
                else if (strcmp(argv[i], "error") == 0)
                {
                    level_val = Sys::Logger::LogLevel::Error;
                    filter_level = &level_val;
                }
            }
            else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc)
            {
                // タイプフィルタ
                i++;
                if (strcmp(argv[i], "kernel") == 0)
                {
                    type_val = Sys::Logger::LogType::Kernel;
                    filter_type = &type_val;
                }
                else if (strcmp(argv[i], "fs") == 0)
                {
                    type_val = Sys::Logger::LogType::FS;
                    filter_type = &type_val;
                }
                else if (strcmp(argv[i], "driver") == 0)
                {
                    type_val = Sys::Logger::LogType::Driver;
                    filter_type = &type_val;
                }
                else if (strcmp(argv[i], "memory") == 0)
                {
                    type_val = Sys::Logger::LogType::Memory;
                    filter_type = &type_val;
                }
                else if (strcmp(argv[i], "app") == 0)
                {
                    type_val = Sys::Logger::LogType::Application;
                    filter_type = &type_val;
                }
            }
            else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc)
            {
                // キーワード検索
                i++;
                keyword = argv[i];
            }
            else if (strcmp(argv[i], "flush") == 0)
            {
                do_flush = true;
            }
        }

        // flush コマンド
        if (do_flush)
        {
            Sys::Logger::g_event_logger->Flush();
            kprintf("Logs flushed to file.\n");
            return;
        }

        // ログ表示 (ページネーション対応)
        constexpr uint32_t kLogsPerPage = 10;
        uint32_t total_logs = Sys::Logger::g_event_logger->GetLogCount(
            filter_level, filter_type, keyword);

        if (total_logs == 0)
        {
            kprintf("No logs found.\n");
            return;
        }

        uint32_t current_page = 0;
        uint32_t total_pages = (total_logs + kLogsPerPage - 1) / kLogsPerPage;

        Sys::Logger::LogEntry entries[kLogsPerPage];

        bool viewing = true;
        while (viewing)
        {
            // 画面クリア (簡易)
            for (int i = 0; i < 25; i++)
                kprintf("\n");

            kprintf("=== Event Log (Page %d/%d, Total: %d) ===\n",
                    current_page + 1, total_pages, total_logs);
            kprintf("Filters: ");
            if (filter_level)
                kprintf("Level=%s ",
                        Sys::Logger::EventLogger::LevelToString(*filter_level));
            if (filter_type)
                kprintf("Type=%s ",
                        Sys::Logger::EventLogger::TypeToString(*filter_type));
            if (keyword)
                kprintf("Keyword=\"%s\"", keyword);
            if (!filter_level && !filter_type && !keyword)
                kprintf("None");
            kprintf("\n");
            kprintf("----------------------------------------\n");

            uint32_t offset = current_page * kLogsPerPage;
            uint32_t count = Sys::Logger::g_event_logger->GetLogs(
                entries, kLogsPerPage, offset, filter_level, filter_type,
                keyword);

            for (uint32_t i = 0; i < count; i++)
            {
                const auto &e = entries[i];

                // ANSI カラーコード: Info=緑背景, Warning=黄背景, Error=赤背景
                // ESC[42m = 緑背景, ESC[43m = 黄背景, ESC[41m = 赤背景
                // ESC[30m = 黒文字, ESC[0m = リセット
                switch (e.level)
                {
                    case Sys::Logger::LogLevel::Info:
                        kprintf("\033[42;30m"); // 緑背景、黒文字
                        break;
                    case Sys::Logger::LogLevel::Warning:
                        kprintf("\033[43;30m"); // 黄背景、黒文字
                        break;
                    case Sys::Logger::LogLevel::Error:
                        kprintf("\033[41;37m"); // 赤背景、白文字
                        break;
                }

                kprintf("[%5s]",
                        Sys::Logger::EventLogger::LevelToString(e.level));
                kprintf("\033[0m"); // リセット

                kprintf("[%6s] %s\n",
                        Sys::Logger::EventLogger::TypeToString(e.type),
                        e.message);
            }

            kprintf("----------------------------------------\n");
            kprintf("[<-] Prev  [->] Next  [Q] Quit\n");

            // キー入力待ち
            bool waiting = true;
            while (waiting)
            {
                g_usb_keyboard->ForceSendTRB();
                char buf[8];
                int len = g_fds[0]->Read(buf, sizeof(buf));
                if (len > 0)
                {
                    for (int i = 0; i < len; i++)
                    {
                        char c = buf[i];
                        if (c == 'q' || c == 'Q')
                        {
                            viewing = false;
                            waiting = false;
                        }
                        else if (c == 0x1B) // ESC sequence for arrows
                        {
                            // 矢印キーの処理 (簡易)
                            // 左矢印: 前ページ
                            if (current_page > 0)
                            {
                                current_page--;
                            }
                            waiting = false;
                        }
                        else if (c == '[') // Part of arrow key sequence
                        {
                            // 次の文字を確認
                        }
                        else if (c == 'C') // 右矢印
                        {
                            if (current_page < total_pages - 1)
                            {
                                current_page++;
                            }
                            waiting = false;
                        }
                        else if (c == 'D') // 左矢印
                        {
                            if (current_page > 0)
                            {
                                current_page--;
                            }
                            waiting = false;
                        }
                        else if (c == 'n' || c == ' ') // 次ページ
                        {
                            if (current_page < total_pages - 1)
                            {
                                current_page++;
                            }
                            waiting = false;
                        }
                        else if (c == 'p') // 前ページ
                        {
                            if (current_page > 0)
                            {
                                current_page--;
                            }
                            waiting = false;
                        }
                    }
                }
            }
        }
    }
    else if (strcmp(argv[0], "sys") == 0)
    {
        kprintf("=============== Sylphia-OS ZERO ===============\n");
        kprintf("Version: v%d.%d.%d-%s\n", System::Version.Major,
                System::Version.Minor, System::Version.Patch,
                System::ReleaseTypeToString());
        kprintf("Build: %04d/%02d/%02d\n", System::BuildInfo.Date.Year,
                System::BuildInfo.Date.Month, System::BuildInfo.Date.Day);
        kprintf("===============================================\n");
    }
    else
    {
        if (g_fds[0]->GetType() == FD_KEYBOARD)
        {
            g_fds[0]->Flush();
        }
        g_usb_keyboard->ForceSendTRB();
        char path[64];
        strcpy(path, "/sys/bin/");
        strcat(path, argv[0]);
        if (!ElfLoader::LoadAndRun(path, argc, argv))
        {
            kprintf("Unknown command: %s\n", argv[0]);
        }
    }
}