TARGET := BOOTX64.EFI
OUTDIR := ../vfat/EFI/BOOT

SRCS := \
  src/elf64.c \
  src/entry.c \
  src/uefi_console.c \
  src/uefi_memory.c

OBJS := $(SRCS:.c=.obj)

CC := clang
LD := lld-link

CFLAGS := -target x86_64-pc-win32-coff -ffreestanding -fno-stack-protector \
          -fshort-wchar -mno-red-zone -Wall -Wextra \
          -Iinclude
LDFLAGS := /subsystem:efi_application /nodefaultlib /entry:EfiMain /machine:x64

all: $(OUTDIR)/$(TARGET)
$(OUTDIR)/$(TARGET): $(OBJS)
	mkdir -p $(OUTDIR)
	$(LD) $(LDFLAGS) /out:$@ $^

%.obj: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(OUTDIR)/$(TARGET)
