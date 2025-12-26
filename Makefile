# --- Tools ---
CLANG     := clang
CLANGPP   := clang++
LD_LLD    := ld.lld
LLD_LINK  := lld-link
NASM      := nasm
CARGO     := cargo

# --- Directories ---
BUILD_DIR  := build
BOOT_DIR   := bootloader
KERNEL_DIR := kernel
APPS_DIR   := apps
APP_DIR  := $(APPS_DIR)/$(APP)
STD_DIR    := std
LINK_DIR   := $(APPS_DIR)/_link
RUST_DIR   := $(KERNEL_DIR)/rust

# --- Rust Configuration ---
RUST_TARGET  := x86_64-unknown-none
RUST_LIB     := $(RUST_DIR)/target/$(RUST_TARGET)/release/libsylphia_rust.a

APP_SRCS := $(wildcard $(APP_DIR)/*.cpp)
KERNEL_ASM_SRCS := $(KERNEL_DIR)/task/context_switch.asm $(KERNEL_DIR)/asmfunc.asm
KERNEL_CPP_SRCS := $(KERNEL_DIR)/main.cpp $(KERNEL_DIR)/cxx.cpp $(KERNEL_DIR)/new.cpp \
                   $(KERNEL_DIR)/app/elf/app_wrapper.cpp $(KERNEL_DIR)/app/elf/elf_loader.cpp \
                   $(KERNEL_DIR)/app/elf/rust_ffi.cpp \
                   $(KERNEL_DIR)/driver/nvme/nvme_driver.cpp \
                   $(KERNEL_DIR)/driver/usb/keyboard/keyboard.cpp $(KERNEL_DIR)/driver/usb/mass_storage/mass_storage.cpp \
                   $(KERNEL_DIR)/driver/usb/xhci.cpp \
                   $(KERNEL_DIR)/fs/fat32/fat32_driver.cpp $(KERNEL_DIR)/fs/fat32/fat32.cpp \
                   $(KERNEL_DIR)/fs/gpt.cpp $(KERNEL_DIR)/fs/installer.cpp \
                   $(KERNEL_DIR)/memory/memory_manager.cpp $(KERNEL_DIR)/pci/pci.cpp \
                   $(KERNEL_DIR)/shell/shell.cpp $(KERNEL_DIR)/sys/init/init.cpp \
                   $(KERNEL_DIR)/sys/logger/logger.cpp $(KERNEL_DIR)/sys/std/file_descriptor.cpp \
                   $(KERNEL_DIR)/sys/sys.cpp $(KERNEL_DIR)/sys/syscall.cpp \
                   $(KERNEL_DIR)/task/idle_task.cpp $(KERNEL_DIR)/task/scheduler.cpp \
                   $(KERNEL_DIR)/task/task_manager.cpp $(KERNEL_DIR)/task/test_task.cpp \
                   $(KERNEL_DIR)/apic.cpp $(KERNEL_DIR)/console.cpp $(KERNEL_DIR)/font.cpp \
                   $(KERNEL_DIR)/graphics.cpp $(KERNEL_DIR)/interrupt.cpp $(KERNEL_DIR)/ioapic.cpp \
                   $(KERNEL_DIR)/keyboard_layout.cpp $(KERNEL_DIR)/paging.cpp \
                   $(KERNEL_DIR)/pic.cpp $(KERNEL_DIR)/printk.cpp $(KERNEL_DIR)/segmentation.cpp
STD_SRCS        := $(STD_DIR)/string.cpp

# --- Object Mapping ---
APP_OBJS := $(APP_SRCS:.cpp=.o)
KERNEL_OBJS := $(KERNEL_ASM_SRCS:%.asm=$(BUILD_DIR)/%.obj) \
               $(KERNEL_CPP_SRCS:%.cpp=$(BUILD_DIR)/%.obj) \
               $(STD_SRCS:%.cpp=$(BUILD_DIR)/%.obj)

# --- Flags ---
# Kernel Flags
KERNEL_FLAGS := -target x86_64-elf -ffreestanding -fno-rtti -fno-exceptions \
                -mno-red-zone -mgeneral-regs-only -I. -I$(KERNEL_DIR) -O2 -Wall
APP_FLAGS := -target x86_64-pc-none-elf -ffreestanding -fno-rtti -fno-exceptions -O2 -Wall
# Bootloader Flags
BOOT_CFLAGS  := -target x86_64-pc-win32-coff -fno-stack-protector -fshort-wchar -mno-red-zone

# --- Targets ---
APP_OUT_DIR := $(BUILD_DIR)/apps
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
BOOT_EFI   := $(BUILD_DIR)/EFI/BOOT/BOOTX64.EFI
START_OBJ  := $(LINK_DIR)/start.o

ifeq ($(OS),Windows_NT)
    MKDIR = if not exist $(subst /,\,$(1)) mkdir $(subst /,\,$(1))
    RMDIR = if exist $(subst /,\,$(1)) rmdir /s /q $(subst /,\,$(1))
    RM    = if exist $(subst /,\,$(1)) del /q $(subst /,\,$(1))
    CLEAN_ALL_OBJ = del /s /q *.obj *.o >nul 2>&1
else
    MKDIR = mkdir -p $(1)
    RMDIR = rm -rf $(1)
    RM    = rm -f $(1)
    CLEAN_ALL_OBJ = find . -type f \( -name "*.o" -o -name "*.obj" \) -delete
endif

# --- Build Rules ---

.PHONY: all bootloader kernel apps app clean rust

# Default: Build everything
all: bootloader kernel apps

# 1. Bootloader
bootloader: $(BOOT_EFI)

$(BOOT_EFI): $(BOOT_DIR)/main.c
	@if not exist $(BUILD_DIR)\EFI\BOOT mkdir $(BUILD_DIR)\EFI\BOOT
	$(CLANG) $(BOOT_CFLAGS) -c $< -o $(BUILD_DIR)/bootloader.obj
	$(LLD_LINK) /subsystem:efi_application /entry:EfiMain /dll /out:$@ $(BUILD_DIR)/bootloader.obj

# 2. Rust Library
rust: $(RUST_LIB)

$(RUST_LIB):
	@cd $(RUST_DIR) && $(CARGO) build --release --target $(RUST_TARGET)

# 3. Kernel (depends on Rust library)
kernel: rust $(KERNEL_ELF)

$(KERNEL_ELF): $(KERNEL_OBJS) $(RUST_LIB)
	$(LD_LLD) -entry KernelMain -z norelro -T $(KERNEL_DIR)/kernel.ld --static -o $@ $(KERNEL_OBJS) $(RUST_LIB)

$(BUILD_DIR)/%.obj: %.cpp
	@if not exist $(subst /,\,$(dir $@)) mkdir $(subst /,\,$(dir $@))
	$(CLANGPP) $(KERNEL_FLAGS) -c $< -o $@

$(BUILD_DIR)/%.obj: %.asm
	@if not exist $(subst /,\,$(dir $@)) mkdir $(subst /,\,$(dir $@))
	$(NASM) -f elf64 $< -o $@

# 4. Applications
app: $(START_OBJ) $(APP_OBJS)
	@if not exist $(subst /,\,$(APP_OUT_DIR)) mkdir $(subst /,\,$(APP_OUT_DIR))
	$(LD_LLD) -T $(LINK_DIR)/linker.ld -o $(APP_OUT_DIR)/$(APP).elf $(START_OBJ) $(APP_OBJS) --entry _start

$(APP_DIR)/%.o: $(APP_DIR)/%.cpp
	$(CLANGPP) $(APP_FLAGS) -c $< -o $@

$(START_OBJ): $(LINK_DIR)/start.asm
	$(NASM) -f elf64 $< -o $@

apps:
	@$(MAKE) app APP=shell --no-print-directory
	@$(MAKE) app APP=test --no-print-directory
	@$(MAKE) app APP=stdio --no-print-directory

# 5. Cleanup
clean:
	@$(call RMDIR,$(BUILD_DIR))
	@$(call RM,$(START_OBJ))
	@cd $(RUST_DIR) && $(CARGO) clean
	@$(CLEAN_ALL_OBJ)