#pragma once
#include <stdint.h>

class ElfLoader
{
public:
    static bool LoadAndRun(const char *filename);
};