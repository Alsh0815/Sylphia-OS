//! ELFファイルパーサー
//!
//! ELF64ファイル形式の解析を行います。

/// ELF識別子のサイズ
pub const EI_NIDENT: usize = 16;

/// ELFマジックナンバー
pub const ELF_MAGIC: [u8; 4] = [0x7F, b'E', b'L', b'F'];

/// プログラムヘッダータイプ: ロード可能セグメント
pub const PT_LOAD: u32 = 1;

/// ELFタイプ: 実行可能ファイル
pub const ET_EXEC: u16 = 2;
/// ELFタイプ: 共有オブジェクト（PIE）
pub const ET_DYN: u16 = 3;

/// マシンタイプ: x86_64
pub const EM_X86_64: u16 = 62;

/// ELF64ファイルヘッダー
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct Elf64Ehdr {
    /// ELF識別子
    pub e_ident: [u8; EI_NIDENT],
    /// ファイルタイプ
    pub e_type: u16,
    /// マシンタイプ
    pub e_machine: u16,
    /// ELFバージョン
    pub e_version: u32,
    /// エントリーポイント
    pub e_entry: u64,
    /// プログラムヘッダーオフセット
    pub e_phoff: u64,
    /// セクションヘッダーオフセット
    pub e_shoff: u64,
    /// フラグ
    pub e_flags: u32,
    /// ELFヘッダーサイズ
    pub e_ehsize: u16,
    /// プログラムヘッダーエントリサイズ
    pub e_phentsize: u16,
    /// プログラムヘッダー数
    pub e_phnum: u16,
    /// セクションヘッダーエントリサイズ
    pub e_shentsize: u16,
    /// セクションヘッダー数
    pub e_shnum: u16,
    /// セクション名文字列テーブルインデックス
    pub e_shstrndx: u16,
}

/// ELF64プログラムヘッダー
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct Elf64Phdr {
    /// セグメントタイプ
    pub p_type: u32,
    /// セグメントフラグ
    pub p_flags: u32,
    /// ファイル内オフセット
    pub p_offset: u64,
    /// 仮想アドレス
    pub p_vaddr: u64,
    /// 物理アドレス
    pub p_paddr: u64,
    /// ファイル内サイズ
    pub p_filesz: u64,
    /// メモリ内サイズ
    pub p_memsz: u64,
    /// アライメント
    pub p_align: u64,
}

/// ELF解析エラー
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ElfError {
    /// ファイルサイズが不足
    FileTooSmall,
    /// 無効なマジックナンバー
    InvalidMagic,
    /// サポートされていないマシンタイプ
    UnsupportedMachine,
    /// 無効なプログラムヘッダー
    InvalidProgramHeader,
    /// カーネルメモリとの重複
    KernelMemoryOverlap,
    /// メモリ確保失敗
    MemoryAllocationFailed,
    /// ファイル読み込み失敗
    FileReadFailed,
}

/// ELFヘッダーを検証する
pub fn validate_elf_header(file_buf: &[u8]) -> Result<&Elf64Ehdr, ElfError> {
    // ファイルサイズチェック
    if file_buf.len() < core::mem::size_of::<Elf64Ehdr>() {
        return Err(ElfError::FileTooSmall);
    }

    // ヘッダーへの参照を取得
    let ehdr = unsafe { &*(file_buf.as_ptr() as *const Elf64Ehdr) };

    // マジックナンバーチェック
    if ehdr.e_ident[0..4] != ELF_MAGIC {
        return Err(ElfError::InvalidMagic);
    }

    // マシンタイプチェック
    if ehdr.e_machine != EM_X86_64 {
        return Err(ElfError::UnsupportedMachine);
    }

    Ok(ehdr)
}

/// プログラムヘッダーのイテレータを取得する
pub fn get_program_headers<'a>(
    file_buf: &'a [u8],
    ehdr: &Elf64Ehdr,
) -> impl Iterator<Item = &'a Elf64Phdr> {
    let phoff = ehdr.e_phoff as usize;
    let phentsize = ehdr.e_phentsize as usize;
    let phnum = ehdr.e_phnum as usize;

    (0..phnum).filter_map(move |i| {
        let offset = phoff + i * phentsize;
        if offset + core::mem::size_of::<Elf64Phdr>() <= file_buf.len() {
            Some(unsafe { &*(file_buf.as_ptr().add(offset) as *const Elf64Phdr) })
        } else {
            None
        }
    })
}
