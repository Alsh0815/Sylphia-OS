#pragma once
#include <stdint.h>

class BlockDevice
{
public:
    virtual ~BlockDevice() = default;

    virtual bool Read(uint64_t lba, void *buffer, uint32_t count) = 0;
    virtual bool Write(uint64_t lba, const void *buffer, uint32_t count) = 0;

    virtual uint32_t GetBlockSize() const = 0;
};