#include "elf_loader.hpp"
#include "elf.hpp"
#include "fs/fat32/fat32_driver.hpp"
#include "memory/memory_manager.hpp"
#include "paging.hpp"
#include "printk.hpp"
#include "cxx.hpp"

extern "C" void EnterUserMode(uint64_t entry_point, uint64_t user_stack_top);

bool ElfLoader::LoadAndRun(const char *filename)
{
    if (!FileSystem::g_fat32_driver)
    {
        kprintf("File System not ready.\n");
        return false;
    }

    // 1. ファイル全体を読み込むためのバッファ確保 (例: 1MB制限)
    // ※本来はファイルサイズを取得してから確保すべきですが、簡易実装として固定サイズにします
    uint32_t buf_size = 1024 * 1024;
    void *file_buf = MemoryManager::Allocate(buf_size);

    // 2. ファイル全体をバッファに読み込む
    // ReadFileは読み込んだバイト数を返します
    uint32_t file_size = FileSystem::g_fat32_driver->ReadFile(filename, file_buf, buf_size);

    if (file_size == 0)
    {
        kprintf("Failed to load file: %s (Not found or empty)\n", filename);
        MemoryManager::Free(file_buf, buf_size);
        return false;
    }

    // バッファの先頭をELFヘッダとして解釈する
    Elf64_Ehdr *ehdr = reinterpret_cast<Elf64_Ehdr *>(file_buf);

    // 3. マジックナンバー確認 (\x7F E L F)
    // ファイルサイズがヘッダより小さい場合はエラー
    if (file_size < sizeof(Elf64_Ehdr) ||
        ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F')
    {
        kprintf("Not an ELF file: %s\n", filename);
        MemoryManager::Free(file_buf, buf_size);
        return false;
    }

    // 4. セグメントのロード (メモリ展開)
    // プログラムヘッダテーブルの位置を取得
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
                kprintf("Error: ELF segment overlaps with Kernel Memory! (%lx)\n", vaddr_start);
                MemoryManager::Free(file_buf, buf_size);
                return false;
            }

            uint64_t mem_size = ph->p_memsz;
            uint64_t file_size_seg = ph->p_filesz;

            // ページ確保
            uint64_t start_page = vaddr_start & ~0xFFF;
            uint64_t end_page = (vaddr_start + mem_size + 0xFFF) & ~0xFFF;
            uint64_t alloc_size = end_page - start_page;

            if (!PageManager::AllocateVirtual(start_page, alloc_size,
                                              PageManager::kPresent | PageManager::kWritable | PageManager::kUser))
            {
                kprintf("Memory allocation failed at %lx\n", vaddr_start);
                MemoryManager::Free(file_buf, buf_size);
                return false;
            }

            // データのコピー
            // file_buf (読み込んだデータ) の該当オフセットから、メモリ (vaddr) へコピー
            uint8_t *src = static_cast<uint8_t *>(file_buf) + ph->p_offset;
            uint8_t *dest = reinterpret_cast<uint8_t *>(vaddr_start);

            memcpy(dest, src, file_size_seg);

            // BSS領域 (ファイルサイズ < メモリサイズ の部分) を0クリア
            if (mem_size > file_size_seg)
            {
                memset(dest + file_size_seg, 0, mem_size - file_size_seg);
            }
        }
    }

    uint64_t entry_point = ehdr->e_entry;

    // 一時バッファ解放
    MemoryManager::Free(file_buf, buf_size);

    // 5. ユーザースタックの準備 (64KB)
    uint64_t stack_addr = 0x70000000;
    uint64_t stack_size = 16 * 4096;

    if (!PageManager::AllocateVirtual(stack_addr - stack_size, stack_size,
                                      PageManager::kPresent | PageManager::kWritable | PageManager::kUser))
    {
        kprintf("Failed to allocate user stack.\n");
        return false;
    }

    kprintf("Starting App at %lx...\n", entry_point);

    // ユーザーモードへ遷移
    EnterUserMode(entry_point, stack_addr);

    return true;
}