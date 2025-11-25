#include "console.hpp"
#include <stdint.h>

Console *g_console = nullptr;

Console::Console(const FrameBufferConfig &config, uint32_t fg_color, uint32_t bg_color)
    : config_(config), fg_color_(fg_color), bg_color_(bg_color),
      cursor_row_(0), cursor_column_(0)
{

    // 画面サイズから最大行数・文字数を計算 (8x16フォント前提)
    columns_ = config_.HorizontalResolution / 8;
    rows_ = config_.VerticalResolution / 16;
}

void Console::PutString(const char *s)
{
    while (*s)
    {
        if (*s == '\n')
        {
            NewLine();
        }
        else if (*s == '\b')
        {
            if (cursor_column_ > 0)
            {
                cursor_column_--;
                WriteAscii(config_, cursor_column_ * 8, cursor_row_ * 16, ' ', fg_color_);
            }
        }
        else if (cursor_column_ < columns_)
        {
            WriteAscii(config_, cursor_column_ * 8, cursor_row_ * 16, *s, fg_color_);
            cursor_column_++;
        }
        if (cursor_column_ >= columns_)
        {
            NewLine();
        }

        s++;
    }
}

void Console::SetColor(uint32_t fg_color, uint32_t bg_color)
{
    fg_color_ = fg_color;
    bg_color_ = bg_color;
}

void Console::NewLine()
{
    cursor_column_ = 0;

    if (cursor_row_ < rows_ - 1)
    {
        // まだ最終行でなければ、単に行を進める
        cursor_row_++;
    }
    else
    {
        // 最終行ならスクロール発生
        Refresh();
    }
}

void Console::Refresh()
{
    // 【スクロール処理】
    // 画面全体を16ピクセル分(1行分)上にコピーする

    uint32_t *base = reinterpret_cast<uint32_t *>(config_.FrameBufferBase);

    // コピーする高さ = 全体 - 1行分
    int copy_height = config_.VerticalResolution - 16;

    // VRAMは遅いので本来はダブルバッファリング等が望ましいが、
    // まずは単純なループコピーで実装する
    for (int y = 0; y < copy_height; ++y)
    {
        for (int x = 0; x < config_.HorizontalResolution; ++x)
        {
            // コピー元: 16ピクセル下の画素
            uint32_t src_index = (y + 16) * config_.PixelsPerScanLine + x;
            // コピー先: 現在の画素
            uint32_t dst_index = y * config_.PixelsPerScanLine + x;

            base[dst_index] = base[src_index];
        }
    }

    // 移動した後、一番下の行を背景色でクリアする
    FillRectangle(config_, 0, (rows_ - 1) * 16, config_.HorizontalResolution, 16, bg_color_);
}