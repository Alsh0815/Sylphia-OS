#include "elf_loader.hpp"
#include "app_wrapper.hpp"
#include "cxx.hpp"
#include "driver/usb/xhci.hpp"
#include "elf.hpp"
#include "fs/fat32/fat32_driver.hpp"
#include "memory/memory_manager.hpp"
#include "paging.hpp"
#include "printk.hpp"
#include "rust_ffi.hpp"
#include "task/scheduler.hpp"
#include "task/task_manager.hpp"
#include <std/string.hpp>

#ifndef USE_RUST_ELF_LOADER
#define USE_RUST_ELF_LOADER 1
#endif

extern "C" void EnterUserMode(uint64_t entry_point, uint64_t user_stack_top,
                              int argc, uint64_t argv_ptr);
extern "C" uint64_t GetCR3();

// アプリ実行状態を追跡するグローバル変数（レガシー互換用）
bool g_app_running = false;

// ELFファイルをメモリにロード
bool ElfLoader::LoadElf(const char *filename, uint64_t *entry_point_out)
{
#if USE_RUST_ELF_LOADER
    // Rust版ELFローダーを使用
    kprintf("[ElfLoader] Using Rust implementation for: %s\n", filename);
    bool result = rust_elf_load(filename, entry_point_out);
    if (!result)
    {
        kprintf("[ElfLoader] Rust loader failed for: %s\n", filename);
    }
    return result;
#else
    // C++版ELFローダー（レガシー）
    kprintf("[ElfLoader] Using C++ implementation for: %s\n", filename);
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

            // 注意: 各プロセスは独自のページテーブルを持つため、
            // 毎回メモリを確保する必要がある
            if (!PageManager::AllocateVirtual(start_page, alloc_size,
                                              PageManager::kPresent |
                                                  PageManager::kWritable |
                                                  PageManager::kUser))
            {
                kprintf("Memory allocation failed at %lx\n", vaddr_start);
                MemoryManager::Free(file_buf, buf_size);
                return false;
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
#endif // USE_RUST_ELF_LOADER
}

// 新API: ELFをロードしてタスクを作成（非同期）
Task *ElfLoader::CreateProcess(const char *filename, int argc, char **argv)
{
    // ファイル名を最初にローカルにコピー（メモリ破壊から保護）
    char filename_copy[256];
    int fn_len = strlen(filename);
    if (fn_len > 255)
        fn_len = 255;
    memcpy(filename_copy, filename, fn_len);
    filename_copy[fn_len] = '\0';

#if USE_RUST_ELF_LOADER
    // ========================================
    // Rust版: ファイル読み込みはカーネルページテーブルで行う
    // ========================================

    kprintf("[ElfLoader] CreateProcess (Rust): %s\n", filename_copy);

    // 1. カーネルページテーブルでファイルを読み込む
    auto *fs = FileSystem::g_system_fs;
    if (!fs)
    {
        kprintf("[ElfLoader] File system not ready\n");
        return nullptr;
    }

    uint32_t buf_size = 1024 * 1024; // 1MB
    void *file_buf = MemoryManager::Allocate(buf_size);
    if (!file_buf)
    {
        kprintf("[ElfLoader] Failed to allocate file buffer\n");
        return nullptr;
    }

    uint32_t file_size = fs->ReadFile(filename_copy, file_buf, buf_size);
    if (file_size == 0)
    {
        kprintf("[ElfLoader] Failed to read file: %s\n", filename_copy);
        MemoryManager::Free(file_buf, buf_size);
        return nullptr;
    }

    kprintf("[ElfLoader] Read %u bytes from %s\n", file_size, filename_copy);

    // 2. アプリタスクを作成（専用ページテーブル付き）
    Task *task =
        TaskManager::CreateAppTask(reinterpret_cast<uint64_t>(AppTaskEntry), 0);
    if (!task)
    {
        kprintf("[ElfLoader] Failed to create app task\n");
        MemoryManager::Free(file_buf, buf_size);
        return nullptr;
    }

    // 3. プロセス用ページテーブルに切り替えてセグメントをロード
    uint64_t entry_point = 0;
    // 重要: syscall呼び出し元（シェル等）のページテーブルを保存
    // GetKernelCR3()ではなくGetCR3()で現在のCR3を保存（C++版と統一）
    uint64_t caller_cr3 = GetCR3();
    PageManager::SwitchPageTable(task->context.cr3);

    bool load_success =
        rust_elf_load_from_buffer(file_buf, file_size, &entry_point);

    // 呼び出し元のページテーブルに戻す
    PageManager::SwitchPageTable(caller_cr3);

    // ファイルバッファを解放
    MemoryManager::Free(file_buf, buf_size);

    if (!load_success)
    {
        kprintf("[ElfLoader] Failed to load ELF segments: %s\n", filename_copy);
        TaskManager::TerminateTask(task);
        return nullptr;
    }

#else
    // ========================================
    // C++版: Rust版と同じ構造に修正
    // ファイル読み込みはカーネルページテーブルで行い、
    // セグメントのロードのみプロセスページテーブルで行う
    // ========================================
    kprintf("[ElfLoader] CreateProcess (C++): %s\n", filename_copy);

    // 1. カーネルページテーブルでファイルを読み込む
    auto *fs = FileSystem::g_system_fs;
    if (!fs)
    {
        kprintf("[ElfLoader] File system not ready\n");
        return nullptr;
    }

    uint32_t buf_size = 1024 * 1024; // 1MB
    void *file_buf = MemoryManager::Allocate(buf_size);
    if (!file_buf)
    {
        kprintf("[ElfLoader] Failed to allocate file buffer\n");
        return nullptr;
    }

    uint32_t file_size = fs->ReadFile(filename_copy, file_buf, buf_size);
    if (file_size == 0)
    {
        kprintf("[ElfLoader] Failed to read file: %s\n", filename_copy);
        MemoryManager::Free(file_buf, buf_size);
        return nullptr;
    }

    // ELFヘッダー検証
    Elf64_Ehdr *ehdr = reinterpret_cast<Elf64_Ehdr *>(file_buf);
    if (file_size < sizeof(Elf64_Ehdr) || ehdr->e_ident[0] != 0x7F ||
        ehdr->e_ident[1] != 'E' || ehdr->e_ident[2] != 'L' ||
        ehdr->e_ident[3] != 'F')
    {
        kprintf("[ElfLoader] Not an ELF file: %s\n", filename_copy);
        MemoryManager::Free(file_buf, buf_size);
        return nullptr;
    }

    // 2. アプリタスクを作成（専用ページテーブル付き）
    Task *task =
        TaskManager::CreateAppTask(reinterpret_cast<uint64_t>(AppTaskEntry), 0);
    if (!task)
    {
        kprintf("[ElfLoader] Failed to create app task\n");
        MemoryManager::Free(file_buf, buf_size);
        return nullptr;
    }

    // 3. プロセス用ページテーブルに切り替えてセグメントをロード
    // 重要: syscall呼び出し元（シェル等）のページテーブルを保存
    // GetKernelCR3()ではなくGetCR3()で現在のCR3を保存
    uint64_t caller_cr3 = GetCR3();
    PageManager::SwitchPageTable(task->context.cr3);

    bool load_success = true;
    uint64_t entry_point = ehdr->e_entry;

    Elf64_Phdr *phdr = reinterpret_cast<Elf64_Phdr *>(
        static_cast<uint8_t *>(file_buf) + ehdr->e_phoff);

    kprintf("[C++ELF] === load_segments START ===\n");
    int segment_count = 0;

    for (int i = 0; i < ehdr->e_phnum; ++i)
    {
        Elf64_Phdr *ph = &phdr[i];

        if (ph->p_type == PT_LOAD)
        {
            segment_count++;

            uint64_t vaddr_start = ph->p_vaddr;
            uint64_t mem_size = ph->p_memsz;
            uint64_t file_size_seg = ph->p_filesz;
            uint64_t offset = ph->p_offset;

            // デバッグログ: セグメント情報
            kprintf("[C++ELF] Seg vaddr=0x%lx\n", vaddr_start);
            kprintf("[C++ELF] Seg offset=0x%lx\n", offset);
            kprintf("[C++ELF] Seg filesz=0x%lx\n", file_size_seg);
            kprintf("[C++ELF] Seg memsz=0x%lx\n", mem_size);

            // ページ確保
            uint64_t start_page = vaddr_start & ~0xFFF;
            uint64_t end_page = (vaddr_start + mem_size + 0xFFF) & ~0xFFF;
            uint64_t alloc_size = end_page - start_page;

            kprintf("[C++ELF] alloc start=0x%lx\n", start_page);
            kprintf("[C++ELF] alloc size=0x%lx\n", alloc_size);

            if (!PageManager::AllocateVirtual(start_page, alloc_size,
                                              PageManager::kPresent |
                                                  PageManager::kWritable |
                                                  PageManager::kUser))
            {
                kprintf("[C++ELF] ERROR: alloc failed at %lx\n", vaddr_start);
                load_success = false;
                break;
            }

            kprintf("[C++ELF] alloc OK, copying...\n");

            // ファイルからロード
            uint8_t *src = static_cast<uint8_t *>(file_buf) + offset;
            uint8_t *dest = reinterpret_cast<uint8_t *>(vaddr_start);

            // デバッグ: コピー元データの先頭16バイトをダンプ
            if (file_size_seg >= 16)
            {
                kprintf("[C++ELF] src bytes: ");
                for (int b = 0; b < 16; b++)
                {
                    kprintf("%02x ", src[b]);
                }
                kprintf("\n");
            }

            memcpy(dest, src, file_size_seg);

            // デバッグ: コピー先データの先頭16バイトをダンプ
            if (file_size_seg >= 16)
            {
                kprintf("[C++ELF] dst bytes: ");
                for (int b = 0; b < 16; b++)
                {
                    kprintf("%02x ", dest[b]);
                }
                kprintf("\n");
            }

            // BSSをゼロクリア
            if (mem_size > file_size_seg)
            {
                memset(dest + file_size_seg, 0, mem_size - file_size_seg);
                kprintf("[C++ELF] BSS cleared=%lu\n", mem_size - file_size_seg);
            }

            kprintf("[C++ELF] Segment loaded OK\n");
        }
    }

    kprintf("[C++ELF] Total segments=%d\n", segment_count);
    kprintf("[C++ELF] === load_segments END ===\n");

    // 呼び出し元のページテーブルに戻す
    PageManager::SwitchPageTable(caller_cr3);

    // ファイルバッファを解放
    MemoryManager::Free(file_buf, buf_size);

    if (!load_success)
    {
        kprintf("[ElfLoader] Failed to load ELF: %s\n", filename_copy);
        TaskManager::TerminateTask(task);
        return nullptr;
    }
#endif

    // エントリーポイントを設定
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
    int name_len = strlen(filename_copy);
    if (name_len > 31)
        name_len = 31;
    memcpy(task->name, filename_copy, name_len);
    task->name[name_len] = '\0';

    // タスクをレディキューに追加
    TaskManager::AddToReadyQueue(task);

    kprintf("[ElfLoader] Created process '%s' (ID=%lu, Entry=%lx, CR3=%lx)\n",
            filename_copy, task->task_id, entry_point, task->context.cr3);

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

            // 注意: レガシーモードでも毎回メモリを確保
            if (!PageManager::AllocateVirtual(start_page, alloc_size,
                                              PageManager::kPresent |
                                                  PageManager::kWritable |
                                                  PageManager::kUser))
            {
                kprintf("Memory allocation failed at %lx\n", vaddr_start);
                MemoryManager::Free(file_buf, buf_size);
                return false;
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