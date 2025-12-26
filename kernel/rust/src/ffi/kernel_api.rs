//! C++カーネルAPIバインディング
//!
//! C++カーネルの機能をRustから呼び出すための extern "C" 宣言

use core::ffi::c_char;

// =============================================================================
// C++カーネルから提供される関数（Rustから呼び出す）
// =============================================================================

extern "C" {
    /// メモリを確保する
    /// size: 確保するバイト数
    /// 戻り値: 確保したメモリへのポインタ（失敗時はnull）
    pub fn memory_allocate(size: u64) -> *mut u8;

    /// メモリを解放する
    /// ptr: 解放するメモリへのポインタ
    /// size: 解放するサイズ
    pub fn memory_free(ptr: *mut u8, size: u64);

    /// 仮想メモリを確保してマップする
    /// vaddr: 仮想アドレス
    /// size: サイズ
    /// flags: ページフラグ
    /// 戻り値: 成功時true
    pub fn page_allocate_virtual(vaddr: u64, size: u64, flags: u64) -> bool;

    /// ファイルを読み込む
    /// filename: ファイル名（NUL終端文字列）
    /// buf: 読み込み先バッファ
    /// buf_size: バッファサイズ
    /// 戻り値: 読み込んだバイト数（0=失敗）
    pub fn fs_read_file(filename: *const c_char, buf: *mut u8, buf_size: u32) -> u32;

    /// カーネルログ出力（kprintf相当）
    pub fn kprintf_rust(msg: *const c_char);
}

// =============================================================================
// ページフラグ定数（PageManagerと同じ値）
// =============================================================================

pub const PAGE_PRESENT: u64 = 1 << 0;
pub const PAGE_WRITABLE: u64 = 1 << 1;
pub const PAGE_USER: u64 = 1 << 2;

// =============================================================================
// デバッグログ出力ヘルパー
// =============================================================================

/// 固定メッセージをログ出力（NUL終端が必要）
pub fn log_msg(msg: &[u8]) {
    if msg.is_empty() {
        return;
    }
    // NUL終端を確認
    if msg[msg.len() - 1] == 0 {
        unsafe {
            kprintf_rust(msg.as_ptr() as *const c_char);
        }
    }
}

/// 64bit値を16進数で出力
pub fn log_hex(prefix: &[u8], value: u64) {
    // バッファ: prefix + "0x" + 16桁hex + "\n" + NUL
    let mut buf = [0u8; 80];
    let mut pos = 0;
    
    // prefixをコピー
    for &b in prefix {
        if b == 0 || pos >= 60 {
            break;
        }
        buf[pos] = b;
        pos += 1;
    }
    
    // "0x"を追加
    buf[pos] = b'0';
    pos += 1;
    buf[pos] = b'x';
    pos += 1;
    
    // 16桁の16進数
    for i in (0..16).rev() {
        let nibble = ((value >> (i * 4)) & 0xF) as u8;
        buf[pos] = if nibble < 10 { b'0' + nibble } else { b'A' + nibble - 10 };
        pos += 1;
    }
    
    // 改行とNUL終端
    buf[pos] = b'\n';
    pos += 1;
    buf[pos] = 0;
    
    unsafe {
        kprintf_rust(buf.as_ptr() as *const c_char);
    }
}

/// デバッグ用: 先頭バイトをhexダンプ
pub fn log_bytes(prefix: &[u8], data: &[u8], max_len: usize) {
    let mut buf = [0u8; 100];
    let mut pos = 0;
    
    // prefixをコピー
    for &b in prefix {
        if b == 0 || pos >= 40 {
            break;
        }
        buf[pos] = b;
        pos += 1;
    }
    
    // バイト数を制限
    let len = core::cmp::min(data.len(), max_len);
    
    for i in 0..len {
        if pos >= 90 {
            break;
        }
        let byte = data[i];
        let hi = (byte >> 4) & 0xF;
        let lo = byte & 0xF;
        buf[pos] = if hi < 10 { b'0' + hi } else { b'A' + hi - 10 };
        pos += 1;
        buf[pos] = if lo < 10 { b'0' + lo } else { b'A' + lo - 10 };
        pos += 1;
        buf[pos] = b' ';
        pos += 1;
    }
    
    // 改行とNUL終端
    buf[pos] = b'\n';
    pos += 1;
    buf[pos] = 0;
    
    unsafe {
        kprintf_rust(buf.as_ptr() as *const c_char);
    }
}

