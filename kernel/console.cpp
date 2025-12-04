#include "console.hpp"
#include <stdint.h>

Console *g_console = nullptr;

Console::Console(const FrameBufferConfig &config, uint32_t fg_color,
                 uint32_t bg_color)
    : config_(config), fg_color_(fg_color), bg_color_(bg_color), cursor_row_(0),
      cursor_column_(0), default_fg_color_(fg_color),
      default_bg_color_(bg_color), state_(kNormal), esc_buf_idx_(0)
{

    // 画面サイズから最大行数・文字数を計算 (8x16フォント前提)
    columns_ = config_.HorizontalResolution / 8;
    rows_ = config_.VerticalResolution / 16;
}

void Console::PutString(const char *s)
{
    while (*s)
    {
        if (state_ != kNormal)
        {
            ProcessEscapeSequence(*s);
        }
        else if (*s == '\033') // ESC
        {
            state_ = kEsc;
        }
        else if (*s == '\n')
        {
            NewLine();
        }
        else if (*s == '\b')
        {
            if (cursor_column_ > 0)
            {
                cursor_column_--;
                WriteAscii(config_, cursor_column_ * 8, cursor_row_ * 16, ' ',
                           bg_color_, bg_color_); // 背景色で消す
            }
        }
        else if (cursor_column_ < columns_)
        {
            WriteAscii(config_, cursor_column_ * 8, cursor_row_ * 16, *s,
                       fg_color_, bg_color_);
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

void Console::Panic(uint32_t fg_color, uint32_t bg_color)
{
    fg_color_ = fg_color;
    bg_color_ = bg_color;

    FillRectangle(config_, 0, 0, config_.HorizontalResolution,
                  config_.VerticalResolution, bg_color_);

    cursor_row_ = 0;
    cursor_column_ = 0;
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
    FillRectangle(config_, 0, (rows_ - 1) * 16, config_.HorizontalResolution,
                  16, bg_color_);
}

void Console::ProcessEscapeSequence(char c)
{
    if (state_ == kEsc)
    {
        if (c == '[')
        {
            state_ = kBracket;
            esc_buf_idx_ = 0;
        }
        else
        {
            state_ = kNormal; // 非対応シーケンス
        }
    }
    else if (state_ == kBracket)
    {
        if (c >= '0' && c <= '9')
        {
            state_ = kParam;
            if (esc_buf_idx_ < (int)sizeof(esc_buf_) - 1)
            {
                esc_buf_[esc_buf_idx_++] = c;
            }
        }
        else if (c == 'm') // パラメータなしの m (0m相当)
        {
            esc_buf_[0] = '0';
            esc_buf_[1] = '\0';
            ExecuteAnsiCommand();
            state_ = kNormal;
        }
        else
        {
            state_ = kNormal; // 不正
        }
    }
    else if (state_ == kParam)
    {
        if (c >= '0' && c <= '9')
        {
            if (esc_buf_idx_ < (int)sizeof(esc_buf_) - 1)
            {
                esc_buf_[esc_buf_idx_++] = c;
            }
        }
        else if (c == ';')
        {
            esc_buf_[esc_buf_idx_] = '\0';
            ExecuteAnsiCommand();
            esc_buf_idx_ = 0; // 次のパラメータへ
        }
        else if (c == 'm')
        {
            esc_buf_[esc_buf_idx_] = '\0';
            ExecuteAnsiCommand();
            state_ = kNormal;
        }
        else
        {
            state_ = kNormal; // 不正
        }
    }
}

void Console::ExecuteAnsiCommand()
{
    int param = 0;
    const char *p = esc_buf_;
    while (*p)
    {
        param = param * 10 + (*p - '0');
        p++;
    }

    // 簡易カラーマップ (RGB)
    // 0:Black, 1:Red, 2:Green, 3:Yellow, 4:Blue, 5:Magenta, 6:Cyan, 7:White
    static const uint32_t kAnsiColors[] = {
        0x000000, // 0: Black
        0xFF0000, // 1: Red
        0x00FF00, // 2: Green
        0xFFFF00, // 3: Yellow
        0x0000FF, // 4: Blue
        0xFF00FF, // 5: Magenta
        0x00FFFF, // 6: Cyan
        0xFFFFFF  // 7: White
    };

    if (param == 0)
    {
        // Reset
        fg_color_ = default_fg_color_;
        bg_color_ = default_bg_color_;
    }
    else if (param >= 30 && param <= 37)
    {
        // Foreground
        fg_color_ = kAnsiColors[param - 30];
    }
    else if (param >= 40 && param <= 47)
    {
        // Background
        bg_color_ = kAnsiColors[param - 40];
    }
}