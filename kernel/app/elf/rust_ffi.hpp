#pragma once
#include <stdint.h>

/**
 * @file rust_ffi.hpp
 * @brief Rust ELFローダーとのFFI定義
 */

extern "C"
{

    // Rust実装のELFローダー
    bool rust_elf_load(const char *filename, uint64_t *entry_point);

    // バッファからELFをロードする（ページテーブル切り替え後に使用）
    bool rust_elf_load_from_buffer(const void *file_buf, uint32_t file_size,
                                   uint64_t *entry_point);

    // カーネルAPI（Rustから呼び出される）
    void *memory_allocate(uint64_t size);
    void memory_free(void *ptr, uint64_t size);
    bool page_allocate_virtual(uint64_t vaddr, uint64_t size, uint64_t flags);
    uint32_t fs_read_file(const char *filename, void *buf, uint32_t buf_size);
    void kprintf_rust(const char *msg);

} // extern "C"
