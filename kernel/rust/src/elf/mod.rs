//! ELFローダーモジュール
//!
//! ELFファイルの解析とメモリへのロードを行います。

mod parser;
mod loader;

// C FFI関数を公開
pub use loader::rust_elf_load;
