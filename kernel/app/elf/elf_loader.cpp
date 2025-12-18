#include "elf_loader.hpp"
#include "app_wrapper.hpp"
#include "cxx.hpp"
#include "driver/usb/xhci.hpp"
#include "elf.hpp"
#include "fs/fat32/fat32_driver.hpp"
#include "memory/memory_manager.hpp"
#include "paging.hpp"
#include "printk.hpp"
#include "task/scheduler.hpp"
#include "task/task_manager.hpp"
#include <std/string.hpp>

extern "C" void EnterUserMode(uint64_t entry_point, uint64_t user_stack_top,
                              int argc, uint64_t argv_ptr);

// アプリ実行状態を追跡するグローバル変数（レガシー互換用）
bool g_app_running = false;

// ELFファイルをメモリにロード
bool ElfLoader::LoadElf(const char *filename, uint64_t *entry_point_out)
{
    auto *fs = FileSystem::g_system_fs;
    if (!fs)
    {
        kprintf("System File System not ready.\n");
        return false;
    }

    uint32_t buf_size = 1024 * 1024;
    void *file_buf = MemoryManager::Allocate(buf_size);

    uint32_t file_size = fs->ReadFile(filename, file_buf, buf_size);

    if (file_size == 0)
    {
        kprintf("Failed to load file: %s (Not found or empty)\n", filename);
        MemoryManager::Free(file_buf, buf_size);
        return false;
    }

    Elf64_Ehdr *ehdr = reinterpret_cast<Elf64_Ehdr *>(file_buf);

    if (file_size < sizeof(Elf64_Ehdr) || ehdr->e_ident[0] != 0x7F ||
        ehdr->e_ident[1] != 'E' || ehdr->e_ident[2] != 'L' ||
        ehdr->e_ident[3] != 'F')
    {
        kprintf("Not an ELF file: %s\n", filename);
        MemoryManager::Free(file_buf, buf_size);
        return false;
    }

    Elf64_Phdr *phdr = reinterpret_cast<Elf64_Phdr *>(
        static_cast<uint8_t *>(file_buf) + ehdr->e_phoff);

    for (int i = 0; i < ehdr->e_phnum; ++i)
    {
        Elf64_Phdr *ph = &phdr[i];

        if (ph->p_type == PT_LOAD)
        {
            uint64_t vaddr_start = ph->p_vaddr;

            if (vaddr_start >= 0x100000 && vaddr_start < 0x400000)
            {
                kprintf(
                    "Error: ELF segment overlaps with Kernel Memory! (%lx)\n",
                    vaddr_start);
                MemoryManager::Free(file_buf, buf_size);
                return false;
            }

            uint64_t mem_size = ph->p_memsz;
            uint64_t file_size_seg = ph->p_filesz;

            // ページ確保
            uint64_t start_page = vaddr_start & ~0xFFF;
            uint64_t end_page = (vaddr_start + mem_size + 0xFFF) & ~0xFFF;
            uint64_t alloc_size = end_page - start_page;

            static uint64_t last_allocated_start = 0;
            static uint64_t last_allocated_end = 0;

            if (start_page < last_allocated_end &&
                end_page > last_allocated_start)
            {
                // 既に確保済み
            }
            else
            {
                if (!PageManager::AllocateVirtual(start_page, alloc_size,
                                                  PageManager::kPresent |
                                                      PageManager::kWritable |
                                                      PageManager::kUser))
                {
                    kprintf("Memory allocation failed at %lx\n", vaddr_start);
                    MemoryManager::Free(file_buf, buf_size);
                    return false;
                }
                last_allocated_start = start_page;
                last_allocated_end = end_page;
            }

            uint8_t *src = static_cast<uint8_t *>(file_buf) + ph->p_offset;
            uint8_t *dest = reinterpret_cast<uint8_t *>(vaddr_start);

            memcpy(dest, src, file_size_seg);

            if (mem_size > file_size_seg)
            {
                memset(dest + file_size_seg, 0, mem_size - file_size_seg);
            }
        }
    }

    *entry_point_out = ehdr->e_entry;
    MemoryManager::Free(file_buf, buf_size);

    return true;
}

// 新API: ELFをロードしてタスクを作成（非同期）
Task *ElfLoader::CreateProcess(const char *filename, int argc, char **argv)
{
    uint64_t entry_point = 0;

    // ELFファイルをロード
    if (!LoadElf(filename, &entry_point))
    {
        return nullptr;
    }

    // アプリタスクを作成
    Task *task =
        TaskManager::CreateTask(reinterpret_cast<uint64_t>(AppTaskEntry));
    if (!task)
    {
        kprintf("[ElfLoader] Failed to create task\n");
        return nullptr;
    }

    // タスクにアプリ情報を設定
    task->is_app = true;
    task->entry_point = entry_point;
    task->argc = argc;

    // argv をコピー（タスク内で使えるように）
    if (argc > 0 && argv)
    {
        task->argv = static_cast<char **>(
            MemoryManager::Allocate(sizeof(char *) * (argc + 1)));
        for (int i = 0; i < argc; ++i)
        {
            int len = strlen(argv[i]) + 1;
            task->argv[i] = static_cast<char *>(MemoryManager::Allocate(len));
            strcpy(task->argv[i], argv[i]);
        }
        task->argv[argc] = nullptr;
    }
    else
    {
        task->argv = nullptr;
    }

    // タスク名を設定（ファイル名から）
    int name_len = strlen(filename);
    if (name_len > 31)
        name_len = 31;
    memcpy(task->name, filename, name_len);
    task->name[name_len] = '\0';

    // タスクをレディキューに追加
    TaskManager::AddToReadyQueue(task);

    kprintf("[ElfLoader] Created process '%s' (ID=%lu, Entry=%lx)\n", filename,
            task->task_id, entry_point);

    return task;
}

// レガシーAPI（同期的実行）- 互換性のため残す
bool ElfLoader::LoadAndRun(const char *filename, int argc, char **argv)
{
    // アプリ実行開始
    g_app_running = true;

    auto *fs = FileSystem::g_system_fs;
    if (!fs)
    {
        kprintf("System File System not ready.\n");
        return false;
    }

    uint32_t buf_size = 1024 * 1024;
    void *file_buf = MemoryManager::Allocate(buf_size);

    uint32_t file_size = fs->ReadFile(filename, file_buf, buf_size);

    if (file_size == 0)
    {
        kprintf("Failed to load file: %s (Not found or empty)\n", filename);
        MemoryManager::Free(file_buf, buf_size);
        return false;
    }

    Elf64_Ehdr *ehdr = reinterpret_cast<Elf64_Ehdr *>(file_buf);

    if (file_size < sizeof(Elf64_Ehdr) || ehdr->e_ident[0] != 0x7F ||
        ehdr->e_ident[1] != 'E' || ehdr->e_ident[2] != 'L' ||
        ehdr->e_ident[3] != 'F')
    {
        kprintf("Not an ELF file: %s\n", filename);
        MemoryManager::Free(file_buf, buf_size);
        return false;
    }

    Elf64_Phdr *phdr = reinterpret_cast<Elf64_Phdr *>(
        static_cast<uint8_t *>(file_buf) + ehdr->e_phoff);

    for (int i = 0; i < ehdr->e_phnum; ++i)
    {
        Elf64_Phdr *ph = &phdr[i];

        if (ph->p_type == PT_LOAD)
        {
            uint64_t vaddr_start = ph->p_vaddr;

            if (vaddr_start >= 0x100000 && vaddr_start < 0x400000)
            {
                kprintf(
                    "Error: ELF segment overlaps with Kernel Memory! (%lx)\n",
                    vaddr_start);
                MemoryManager::Free(file_buf, buf_size);
                return false;
            }

            uint64_t mem_size = ph->p_memsz;
            uint64_t file_size_seg = ph->p_filesz;

            uint64_t start_page = vaddr_start & ~0xFFF;
            uint64_t end_page = (vaddr_start + mem_size + 0xFFF) & ~0xFFF;
            uint64_t alloc_size = end_page - start_page;

            static uint64_t last_allocated_start = 0;
            static uint64_t last_allocated_end = 0;

            if (start_page < last_allocated_end &&
                end_page > last_allocated_start)
            {
                // skip
            }
            else
            {
                if (!PageManager::AllocateVirtual(start_page, alloc_size,
                                                  PageManager::kPresent |
                                                      PageManager::kWritable |
                                                      PageManager::kUser))
                {
                    kprintf("Memory allocation failed at %lx\n", vaddr_start);
                    MemoryManager::Free(file_buf, buf_size);
                    return false;
                }
                last_allocated_start = start_page;
                last_allocated_end = end_page;
            }

            uint8_t *src = static_cast<uint8_t *>(file_buf) + ph->p_offset;
            uint8_t *dest = reinterpret_cast<uint8_t *>(vaddr_start);

            memcpy(dest, src, file_size_seg);

            if (mem_size > file_size_seg)
            {
                memset(dest + file_size_seg, 0, mem_size - file_size_seg);
            }
        }
    }

    uint64_t entry_point = ehdr->e_entry;
    MemoryManager::Free(file_buf, buf_size);

    uint64_t stack_addr = 0x70000000;
    uint64_t stack_size = 16 * 4096;

    if (!PageManager::AllocateVirtual(stack_addr - stack_size, stack_size,
                                      PageManager::kPresent |
                                          PageManager::kWritable |
                                          PageManager::kUser))
    {
        kprintf("Failed to allocate user stack.\n");
        return false;
    }

    uint64_t sp = stack_addr;

    uint64_t user_argv_ptrs[32];

    for (int i = 0; i < argc; ++i)
    {
        int len = strlen(argv[i]) + 1;
        sp -= len;
        strcpy((char *)sp, argv[i]);
        user_argv_ptrs[i] = sp;
    }

    sp = sp & ~0xF;

    sp -= 8;
    *(uint64_t *)sp = 0;

    for (int i = argc - 1; i >= 0; --i)
    {
        sp -= 8;
        *(uint64_t *)sp = user_argv_ptrs[i];
    }

    uint64_t argv_ptr = sp;

    kprintf("Starting App at %lx with argc=%d\n", entry_point, argc);
    kprintf("  SP: %lx, argv_ptr: %lx\n", sp, argv_ptr);

    EnterUserMode(entry_point, sp, argc, argv_ptr);

    g_app_running = false;

    __asm__ volatile("sti");

    return true;
}