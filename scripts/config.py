class Compiler:
    CARGO_PATH = "cargo"        # Rust Compiler
    CLANG_PATH = "clang"        # C Compiler
    CLANGXX_PATH = "clang++"    # C++ Compiler
    NASM_PATH = "nasm"          # Assembler

class Linker:
    LD_LLD_PATH = "ld.lld"      # LLD Linker
    LLD_LINK_PATH = "lld-link"  # LLD Linker

class QEMU:
    QEMU_ARM_PATH = "qemu-system-arm"           # AArch32
    QEMU_AARCH64_PATH = "qemu-system-aarch64"   # AArch64
    QEMU_I386_PATH = "qemu-system-i386"         # x86
    QEMU_X64_PATH = "qemu-system-x86_64"        # x86_64

    QEMU_IMG = "qemu-img"                       # QEMU Image


QEMU_MEMORY = "512M"

NVME_IMG_SIZE = 2 * 1024 * 1024 * 1024  # 2GB