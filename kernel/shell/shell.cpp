#include <std/string.hpp>
#include "app/elf/elf_loader.hpp"
#include "fs/fat32/fat32_driver.hpp"
#include "memory/memory_manager.hpp"
#include "shell/shell.hpp"
#include "console.hpp"
#include "printk.hpp"

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

    char *argv[32]; // Max 32 arguments
    int argc = 0;

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

    if (strcmp(argv[0], "cat") == 0)
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
        uint32_t bytes_read = FileSystem::g_fat32_driver->ReadFile(filename, buf, buf_size);
        if (bytes_read > 0)
        {
            if (bytes_read < buf_size)
                buf[bytes_read] = '\0';
            else
                buf[buf_size - 1] = '\0';

            kprintf("%s\n", buf);
        }
        else
        {
            kprintf("Error: File not found or empty.\n");
        }

        MemoryManager::Free(buf, buf_size);
    }
    else if (strcmp(argv[0], "clear") == 0)
    {
        for (int i = 0; i < 30; i++)
            kprintf("\n");
    }
    else if (strcmp(argv[0], "echo") == 0)
    {
        kprintf("%s\n", argv[1]);
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
    else if (strcmp(argv[0], "whoami") == 0)
    {
        kprintf("root\n");
    }
    else
    {
        char path[64];
        strcpy(path, "/sys/bin/");
        strcat(path, argv[0]);
        if (!ElfLoader::LoadAndRun(path, argc, argv))
        {
            kprintf("Unknown command: %s\n", argv[0]);
        }
    }
}