TARGET := kernel.elf
OUTBIN := ../vfat/kernel/kernel.bin

CXX := clang++
LD  := ld.lld

CXXFLAGS := -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector \
            -fno-plt -mno-red-zone -O2 -Wall -Wextra \
            -std=c++20 -Iinclude
LDFLAGS  := -nostdlib -T linker.ld

SRCS := src/kernel.cpp src/console.cpp src/idt.cpp src/paging.cpp src/pmm.cpp
OBJS := $(SRCS:.cpp=.o)

all: $(OUTBIN)

$(OUTBIN): $(TARGET)
	mkdir -p ../vfat/kernel
	cp -f $< $@

$(TARGET): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
