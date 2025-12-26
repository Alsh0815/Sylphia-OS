//! ELFローダー
//!
//! ELFファイルをメモリにロードする機能を提供します。

use core::ffi::c_char;
use core::ptr;
use super::parser::{self, ElfError, Elf64Ehdr, PT_LOAD};
use crate::ffi::kernel_api::{
    memory_allocate, memory_free, page_allocate_virtual, fs_read_file,
    PAGE_PRESENT, PAGE_WRITABLE, PAGE_USER,
};

/// ファイル読み込みバッファサイズ（1MB）
const FILE_BUF_SIZE: u64 = 1024 * 1024;

/// カーネルメモリ領域の開始アドレス
const KERNEL_MEM_START: u64 = 0x100000;
/// カーネルメモリ領域の終了アドレス
const KERNEL_MEM_END: u64 = 0x400000;

/// ELFファイルをロードする（ファイル名から）
///
/// # Safety
/// filenameは有効なNUL終端文字列へのポインタでなければならない
/// entry_point_outは有効なu64へのポインタでなければならない
#[no_mangle]
pub unsafe extern "C" fn rust_elf_load(
    filename: *const c_char,
    entry_point_out: *mut u64,
) -> bool {
    match load_elf_internal(filename, entry_point_out) {
        Ok(()) => true,
        Err(_e) => false,
    }
}

/// バッファからELFをロードする
/// 
/// この関数はファイル読み込み済みのバッファからELFをロードします。
/// CreateProcessでページテーブル切り替え後に使用することを想定しています。
///
/// # Arguments
/// * `file_buf` - ファイル内容を格納したバッファ
/// * `file_size` - バッファ内のデータサイズ
/// * `entry_point_out` - エントリーポイントを格納するポインタ
///
/// # Safety
/// file_bufは有効なメモリへのポインタでなければならない
/// entry_point_outは有効なu64へのポインタでなければならない
#[no_mangle]
pub unsafe extern "C" fn rust_elf_load_from_buffer(
    file_buf: *const u8,
    file_size: u32,
    entry_point_out: *mut u64,
) -> bool {
    if file_buf.is_null() || file_size == 0 {
        return false;
    }

    let file_slice = core::slice::from_raw_parts(file_buf, file_size as usize);

    match load_segments_and_get_entry(file_slice, entry_point_out) {
        Ok(()) => true,
        Err(_e) => false,
    }
}

/// バッファからセグメントをロードしてエントリーポイントを取得
unsafe fn load_segments_and_get_entry(
    file_buf: &[u8],
    entry_point_out: *mut u64,
) -> Result<(), ElfError> {
    // ELFヘッダーの検証
    let ehdr = parser::validate_elf_header(file_buf)?;

    // セグメントをロード
    load_segments(file_buf, ehdr)?;

    // エントリーポイントを設定
    let entry = ehdr.e_entry;
    *entry_point_out = entry;

    Ok(())
}

/// ELFロードの内部実装（ファイルから読み込む版）
unsafe fn load_elf_internal(
    filename: *const c_char,
    entry_point_out: *mut u64,
) -> Result<(), ElfError> {
    // ファイル読み込みバッファを確保
    let file_buf = memory_allocate(FILE_BUF_SIZE);
    if file_buf.is_null() {
        return Err(ElfError::MemoryAllocationFailed);
    }

    // ファイルを読み込む
    let file_size = fs_read_file(filename, file_buf, FILE_BUF_SIZE as u32);
    if file_size == 0 {
        memory_free(file_buf, FILE_BUF_SIZE);
        return Err(ElfError::FileReadFailed);
    }

    // バッファをスライスとして扱う
    let file_slice = core::slice::from_raw_parts(file_buf, file_size as usize);

    // ELFヘッダーの検証
    let ehdr = match parser::validate_elf_header(file_slice) {
        Ok(h) => h,
        Err(e) => {
            memory_free(file_buf, FILE_BUF_SIZE);
            return Err(e);
        }
    };

    // セグメントをロード
    if let Err(e) = load_segments(file_slice, ehdr) {
        memory_free(file_buf, FILE_BUF_SIZE);
        return Err(e);
    }

    // エントリーポイントを設定
    *entry_point_out = ehdr.e_entry;

    // ファイルバッファを解放
    memory_free(file_buf, FILE_BUF_SIZE);

    Ok(())
}

/// PT_LOADセグメントをメモリにロードする
unsafe fn load_segments(file_buf: &[u8], ehdr: &Elf64Ehdr) -> Result<(), ElfError> {
    use crate::ffi::kernel_api::{log_msg, log_hex, log_bytes};
    
    log_msg(b"[RustELF] === load_segments START ===\n\0");
    
    let mut segment_count = 0u32;
    
    for phdr in parser::get_program_headers(file_buf, ehdr) {
        if phdr.p_type != PT_LOAD {
            continue;
        }
        
        segment_count += 1;
        
        let vaddr_start = phdr.p_vaddr;
        let mem_size = phdr.p_memsz;
        let file_size = phdr.p_filesz;
        let offset = phdr.p_offset;

        // デバッグログ: セグメント情報
        log_hex(b"[RustELF] Seg vaddr=", vaddr_start);
        log_hex(b"[RustELF] Seg offset=", offset);
        log_hex(b"[RustELF] Seg filesz=", file_size);
        log_hex(b"[RustELF] Seg memsz=", mem_size);

        // カーネルメモリとの重複チェック
        if vaddr_start >= KERNEL_MEM_START && vaddr_start < KERNEL_MEM_END {
            log_msg(b"[RustELF] ERROR: kernel overlap\n\0");
            return Err(ElfError::KernelMemoryOverlap);
        }

        // ページアライメント計算
        let start_page = vaddr_start & !0xFFF;
        let end_page = (vaddr_start + mem_size + 0xFFF) & !0xFFF;
        let alloc_size = end_page - start_page;

        log_hex(b"[RustELF] alloc start=", start_page);
        log_hex(b"[RustELF] alloc size=", alloc_size);

        // 仮想メモリを確保
        let flags = PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        if !page_allocate_virtual(start_page, alloc_size, flags) {
            log_msg(b"[RustELF] ERROR: alloc failed\n\0");
            return Err(ElfError::MemoryAllocationFailed);
        }
        
        log_msg(b"[RustELF] alloc OK, copying...\n\0");

        // セグメントデータをコピー
        let src = file_buf.as_ptr().add(offset as usize);
        let dest = vaddr_start as *mut u8;
        
        // デバッグ: コピー元データの先頭16バイトをダンプ
        if file_size >= 16 {
            let src_slice = core::slice::from_raw_parts(src, 16);
            log_bytes(b"[RustELF] src bytes: ", src_slice, 16);
        }

        ptr::copy_nonoverlapping(src, dest, file_size as usize);
        
        // デバッグ: コピー先データの先頭16バイトをダンプ
        if file_size >= 16 {
            let dest_slice = core::slice::from_raw_parts(dest, 16);
            log_bytes(b"[RustELF] dst bytes: ", dest_slice, 16);
        }

        // BSS領域をゼロクリア
        if mem_size > file_size {
            let bss_start = dest.add(file_size as usize);
            let bss_size = (mem_size - file_size) as usize;
            ptr::write_bytes(bss_start, 0, bss_size);
            log_hex(b"[RustELF] BSS cleared=", bss_size as u64);
        }
        
        log_msg(b"[RustELF] Segment loaded OK\n\0");
    }
    
    log_hex(b"[RustELF] Total segments=", segment_count as u64);
    log_msg(b"[RustELF] === load_segments END ===\n\0");

    Ok(())
}
