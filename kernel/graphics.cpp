#include "graphics.hpp"

// font.cpp で定義されている関数
const uint8_t *GetFont(char c);

void FillRectangle(const FrameBufferConfig &config, int x, int y, int w, int h,
                   uint32_t color)
{
    uint32_t *base = reinterpret_cast<uint32_t *>(config.FrameBufferBase);

    for (int dy = 0; dy < h; ++dy)
    {
        int current_y = y + dy;
        if (current_y >= config.VerticalResolution)
            break;

        for (int dx = 0; dx < w; ++dx)
        {
            int current_x = x + dx;
            if (current_x >= config.HorizontalResolution)
                break;

            uint32_t index = current_y * config.PixelsPerScanLine + current_x;
            base[index] = color;
        }
    }
}

void WriteAscii(const FrameBufferConfig &config, int x, int y, char c,
                uint32_t fg_color, uint32_t bg_color)
{
    const uint8_t *font = GetFont(c);
    if (!font)
        return;

    uint32_t *base = reinterpret_cast<uint32_t *>(config.FrameBufferBase);

    // 16行分ループ
    for (int dy = 0; dy < 16; ++dy)
    {
        // y座標が画面外ならスキップ
        if (y + dy >= config.VerticalResolution)
            break;

        // 該当行のビットマップデータを取得
        uint8_t row_bitmap = font[dy];

        // 8列(ビット)分ループ
        for (int dx = 0; dx < 8; ++dx)
        {
            if (x + dx >= config.HorizontalResolution)
                break;

            uint32_t index = (y + dy) * config.PixelsPerScanLine + (x + dx);
            // 最上位ビット(左端)から順にチェック: (1 << (7-dx))
            if ((row_bitmap >> (7 - dx)) & 1)
            {
                // ビットが立っていれば前景色で描画
                base[index] = fg_color;
            }
            else
            {
                // ビットが立っていなければ背景色で描画
                base[index] = bg_color;
            }
        }
    }
}

void WriteString(const FrameBufferConfig &config, int x, int y, const char *s,
                 uint32_t fg_color, uint32_t bg_color)
{
    int cursor_x = x;
    while (*s)
    {
        WriteAscii(config, cursor_x, y, *s, fg_color, bg_color);
        cursor_x += 8; // 1文字幅(8px)進める
        s++;
    }
}