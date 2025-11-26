#pragma once
#include <stdint.h>

struct FileBuffer
{
    void *buffer;
    uint64_t size;
};

struct BootVolumeConfig
{
    FileBuffer kernel_file;
    FileBuffer bootloader_file;
};