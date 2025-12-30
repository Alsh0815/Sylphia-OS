class Compiler:
    CARGO_PATH = "cargo"        # Rust Compiler
    CLANG_PATH = "clang"        # C Compiler
    CLANGXX_PATH = "clang++"    # C++ Compiler
    NASM_PATH = "nasm"          # Assembler

class Linker:
    LD_LLD_PATH = "ld.lld"      # LLD Linker
    LLD_LINK_PATH = "lld-link"  # LLD Linker

class QEMU:
    QEMU_AARCH64_PATH = "qemu-system-aarch64"   # AArch64
    QEMU_X64_PATH = "qemu-system-x86_64"        # x86_64
    QEMU_RISCV64_PATH = "qemu-system-riscv64"   # RISC-V 64bit

    QEMU_IMG = "qemu-img"                       # QEMU Image


QEMU_MEMORY = "512M"

NVME_IMG_SIZE = 2 * 1024 * 1024 * 1024  # 2GB


# アーキテクチャ別設定
ARCH_CONFIG = {
    "x86_64": {
        "clang_target": "x86_64-elf",
        "clang_bootloader_target": "x86_64-pc-win32-coff",
        "linker_script": "kernel.ld",
        "nasm_format": "elf64",
        "rust_target": "x86_64-unknown-none",
        "extra_cflags": ["-mno-red-zone", "-mgeneral-regs-only"],
        "bootloader_output": "BOOTX64.EFI",
        "ovmf_code": "x86_64/OVMF_CODE.fd",
        "ovmf_vars": "x86_64/OVMF_VARS.fd",
        "use_nasm": True,
        "qemu_path": QEMU.QEMU_X64_PATH,
        "qemu_machine": None,  # デフォルトマシン
        "qemu_cpu": None,
    },
    "aarch64": {
        "clang_target": "aarch64-unknown-elf",
        "clang_bootloader_target": "aarch64-unknown-windows",
        "linker_script": "kernel_aarch64.ld",
        "rust_target": "aarch64-unknown-none",
        "extra_cflags": [],
        "bootloader_output": "BOOTAA64.EFI",
        "ovmf_code": "aarch64/AARCH64_QEMU_EFI.fd",
        "ovmf_vars": "aarch64/AARCH64_QEMU_VARS.fd",
        "use_nasm": False,
        "qemu_path": QEMU.QEMU_AARCH64_PATH,
        "qemu_machine": "virt",
        "qemu_cpu": "cortex-a72",
    },
    "riscv64": {
        "clang_target": "riscv64-unknown-elf",
        "clang_bootloader_target": "riscv64-unknown-elf",
        "linker_script": "kernel.ld",
        "rust_target": "riscv64gc-unknown-none-elf",
        "extra_cflags": ["-march=rv64gc", "-mabi=lp64d"],
        "bootloader_output": "BOOTRISCV64.EFI",
        "ovmf_code": "risc-v/RISCV64_VIRT.fd",
        "ovmf_vars": None,
        "use_nasm": False,
        "qemu_path": QEMU.QEMU_RISCV64_PATH,
        "qemu_machine": "virt",
        "qemu_cpu": None,
    },
}