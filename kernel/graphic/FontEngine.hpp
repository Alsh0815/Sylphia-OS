#pragma once

#include <stdint.h>

const uint32_t *Char2Bmp(const char c, uint32_t *buf);
const uint32_t *Str2Bmp(const char *str, uint32_t *buf);