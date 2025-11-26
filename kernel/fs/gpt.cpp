#include <stddef.h>
#include "gpt.hpp"

namespace FileSystem
{

    // 標準的なCRC32アルゴリズム
    uint32_t CalculateCRC32(const void *buffer, size_t length)
    {
        const uint8_t *p = static_cast<const uint8_t *>(buffer);
        uint32_t crc = 0xFFFFFFFF;

        for (size_t i = 0; i < length; ++i)
        {
            crc ^= p[i];
            for (int j = 0; j < 8; ++j)
            {
                if (crc & 1)
                {
                    crc = (crc >> 1) ^ 0xEDB88320;
                }
                else
                {
                    crc >>= 1;
                }
            }
        }
        return ~crc;
    }

}