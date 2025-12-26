//! Sylphia-OS Rust カーネルコンポーネント
//!
//! このクレートはC++カーネルと連携するRustモジュールを提供します。
//! 現在はELFローダー機能を実装しています。

#![no_std]
#![allow(dead_code)]

use core::panic::PanicInfo;

pub mod ffi;
pub mod elf;

/// パニックハンドラー（no_std環境で必須）
#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}
