# --- Tools ---
CLANG     := clang
CLANGPP   := clang++
LD_LLD    := ld.lld
LLD_LINK  := lld-link
NASM      := nasm

# --- Directories ---
BUILD_DIR  := build
BOOT_DIR   := bootloader
KERNEL_DIR := kernel
APPS_DIR   := apps
STD_DIR    := std
LINK_DIR   := $(APPS_DIR)/_link

# --- Kernel Source List (from build_kernel.ps1) ---
KERNEL_ASM_SRCS := $(KERNEL_DIR)/task/context_switch.asm $(KERNEL_DIR)/asmfunc.asm
KERNEL_CPP_SRCS := $(KERNEL_DIR)/main.cpp $(KERNEL_DIR)/cxx.cpp $(KERNEL_DIR)/new.cpp \
                   $(KERNEL_DIR)/app/elf/app_wrapper.cpp $(KERNEL_DIR)/app/elf/elf_loader.cpp \
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
KERNEL_OBJS := $(KERNEL_ASM_SRCS:%.asm=$(BUILD_DIR)/%.obj) \
               $(KERNEL_CPP_SRCS:%.cpp=$(BUILD_DIR)/%.obj) \
               $(STD_SRCS:%.cpp=$(BUILD_DIR)/%.obj)

# --- Flags ---
# Kernel Flags
KERNEL_FLAGS := -target x86_64-elf -ffreestanding -fno-rtti -fno-exceptions \
                -mno-red-zone -mgeneral-regs-only -I. -I$(KERNEL_DIR) -O2 -Wall
# Bootloader Flags
BOOT_CFLAGS  := -target x86_64-pc-win32-coff -fno-stack-protector -fshort-wchar -mno-red-zone

# --- Targets ---
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
BOOT_EFI   := $(BUILD_DIR)/EFI/BOOT/BOOTX64.EFI
START_OBJ  := $(LINK_DIR)/start.o

# --- Build Rules ---

.PHONY: all bootloader kernel apps app clean

# Default: Build everything
all: bootloader kernel apps

# 1. Bootloader
bootloader: $(BOOT_EFI)

$(BOOT_EFI): $(BOOT_DIR)/main.c
	@if not exist $(BUILD_DIR)\EFI\BOOT mkdir $(BUILD_DIR)\EFI\BOOT
	$(CLANG) $(BOOT_CFLAGS) -c $< -o $(BUILD_DIR)/bootloader.obj
	$(LLD_LINK) /subsystem:efi_application /entry:EfiMain /dll /out:$@ $(BUILD_DIR)/bootloader.obj

# 2. Kernel
kernel: $(KERNEL_ELF)

$(KERNEL_ELF): $(KERNEL_OBJS)
	$(LD_LLD) -entry KernelMain -z norelro -T $(KERNEL_DIR)/kernel.ld --static -o $@ $^

$(BUILD_DIR)/%.obj: %.cpp
	@if not exist $(subst /,\,$(dir $@)) mkdir $(subst /,\,$(dir $@))
	$(CLANGPP) $(KERNEL_FLAGS) -c $< -o $@

$(BUILD_DIR)/%.obj: %.asm
	@if not exist $(subst /,\,$(dir $@)) mkdir $(subst /,\,$(dir $@))
	$(NASM) -f elf64 $< -o $@

# 3. Applications
# Usage: make app APP=shell
app: $(START_OBJ)
	@if not exist $(BUILD_DIR)\apps mkdir $(BUILD_DIR)\apps
	@powershell -ExecutionPolicy Bypass -File ./build_scripts/build_app.ps1 $(APP)

$(START_OBJ): $(LINK_DIR)/start.asm
	$(NASM) -f elf64 $< -o $@

apps:
	@make app APP=shell
	@make app APP=test
	@make app APP=stdio

# 4. Cleanup
clean:
	@if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)
	@if exist $(START_OBJ) del $(START_OBJ)