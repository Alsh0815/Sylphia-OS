/**
 * @file rust_ffi.cpp
 * @brief C++カーネルとRust実装を接続するFFIラッパー
 *
 * このファイルは、RustからC++カーネルの機能を呼び出すための
 * extern "C" 関数を提供します。
 */

#include "rust_ffi.hpp"
#include "fs/fat32/fat32_driver.hpp"
#include "memory/memory_manager.hpp"
#include "paging.hpp"
#include "printk.hpp"

// =============================================================================
// Rustから呼び出されるカーネルAPI
// =============================================================================

extern "C"
{

    /**
     * @brief メモリを確保する
     * @param size 確保するバイト数
     * @return 確保したメモリへのポインタ（失敗時はnullptr）
     */
    void *memory_allocate(uint64_t size)
    {
        return MemoryManager::Allocate(static_cast<size_t>(size));
    }

    /**
     * @brief メモリを解放する
     * @param ptr 解放するメモリへのポインタ
     * @param size 解放するサイズ
     */
    void memory_free(void *ptr, uint64_t size)
    {
        MemoryManager::Free(ptr, static_cast<size_t>(size));
    }

    /**
     * @brief 仮想メモリを確保してマップする
     * @param vaddr 仮想アドレス
     * @param size サイズ
     * @param flags ページフラグ
     * @return 成功時true
     */
    bool page_allocate_virtual(uint64_t vaddr, uint64_t size, uint64_t flags)
    {
        return PageManager::AllocateVirtual(vaddr, static_cast<size_t>(size),
                                            flags);
    }

    /**
     * @brief ファイルを読み込む
     * @param filename ファイル名（NUL終端文字列）
     * @param buf 読み込み先バッファ
     * @param buf_size バッファサイズ
     * @return 読み込んだバイト数（0=失敗）
     */
    uint32_t fs_read_file(const char *filename, void *buf, uint32_t buf_size)
    {
        auto *fs = FileSystem::g_system_fs;
        if (!fs)
        {
            return 0;
        }
        return fs->ReadFile(filename, buf, buf_size);
    }

    /**
     * @brief カーネルログ出力
     * @param msg 出力するメッセージ（NUL終端文字列）
     */
    void kprintf_rust(const char *msg)
    {
        kprintf("%s", msg);
    }

} // extern "C"
