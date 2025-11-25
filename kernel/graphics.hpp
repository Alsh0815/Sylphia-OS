#pragma once
#include <stdint.h>

struct FrameBufferConfig
{
    uint64_t FrameBufferBase;
    uint64_t FrameBufferSize;
    uint32_t HorizontalResolution;
    uint32_t VerticalResolution;
    uint32_t PixelsPerScanLine;
};

void FillRectangle(const FrameBufferConfig &config, int x, int y, int w, int h, uint32_t color);

// 指定座標(x, y)に文字cを色colorで描画するs
void WriteAscii(const FrameBufferConfig &config, int x, int y, char c, uint32_t color);

// 文字列を描画する
void WriteString(const FrameBufferConfig &config, int x, int y, const char *s, uint32_t color);