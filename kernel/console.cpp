#include "console.hpp"
#include "graphic/FontEngine.hpp"
#include "io.hpp"
#include "sys/sys.hpp"
#include <stdint.h>

// シリアルポート (COM1) のベースアドレス
#define SERIAL_COM1_PORT 0x3F8

#if SYLPHIA_DEBUG_ENABLED
// シリアルポートに1文字送信
static inline void serial_putchar(char c)
{
    // 送信バッファが空になるまで待機
    while ((IoIn8(SERIAL_COM1_PORT + 5) & 0x20) == 0)
        ;
    IoOut8(SERIAL_COM1_PORT, c);
}
#endif

Console *g_console = nullptr;

Console::Console(Graphic::LowLayerRenderer &llr, uint32_t fg_color,
                 uint32_t bg_color)
    : llr_(llr), fg_color_(fg_color), bg_color_(bg_color), cursor_row_(0),
      cursor_column_(0), default_fg_color_(fg_color),
      default_bg_color_(bg_color), state_(kNormal), esc_buf_idx_(0)
{

    // 画面サイズから最大行数・文字数を計算 (8x16フォント前提)
    columns_ = Graphic::GetDisplayWidth() / 8;
    rows_ = Graphic::GetDisplayHeight() / 16;
}

void Console::WriteChar(int x, int y, char c, uint32_t fg_color,
                        uint32_t bg_color)
{
    // FontEngineを使用して文字をビットマップに変換
    uint32_t char_bitmap[8 * 16];
    const uint32_t *bmp = Char2Bmp(c, char_bitmap);
    if (!bmp)
        return;

    // 色を適用したビットマップを作成
    uint32_t colored_bitmap[8 * 16];
    for (int i = 0; i < 8 * 16; ++i)
    {
        colored_bitmap[i] =
            (char_bitmap[i] == 0xFFFFFFFF) ? fg_color : bg_color;
    }

    // LowLayerRendererで描画
    llr_.WriteBitmap(x, y, 8, 16, colored_bitmap);
}

void Console::PutString(const char *s)
{
    while (*s)
    {
#if SYLPHIA_DEBUG_ENABLED
        // シリアルポートには全文字を出力（エスケープシーケンス含む）
        serial_putchar(*s);
#endif

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
                WriteChar(cursor_column_ * 8, cursor_row_ * 16, ' ', bg_color_,
                          bg_color_); // 背景色で消す
            }
        }
        else if (cursor_column_ < columns_)
        {
            WriteChar(cursor_column_ * 8, cursor_row_ * 16, *s, fg_color_,
                      bg_color_);
            cursor_column_++;
        }

        if (cursor_column_ >= columns_)
        {
            NewLine();
        }

        s++;
    }
    // 文字列出力完了後にバッファをフラッシュ
    llr_.Flush();
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

    llr_.WriteRect(0, 0, Graphic::GetDisplayWidth(),
                   Graphic::GetDisplayHeight(), bg_color_);

    cursor_row_ = 0;
    cursor_column_ = 0;

    llr_.Flush();
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
    // STANDARDモードではフレームバッファに直接描画されているため
    // GetDisplayBufferでバッファを直接操作する

    uint32_t *buffer = Graphic::GetDisplayBuffer();
    if (!buffer)
        return;

    uint64_t display_width = Graphic::GetDisplayWidth();
    uint64_t display_height = Graphic::GetDisplayHeight();

    // コピーする高さ = 全体 - 1行分
    uint64_t copy_height = display_height - 16;

    // VRAMは遅いので本来はダブルバッファリング等が望ましいが、
    // まずは単純なループコピーで実装する
    for (uint64_t y = 0; y < copy_height; ++y)
    {
        for (uint64_t x = 0; x < display_width; ++x)
        {
            // コピー元: 16ピクセル下の画素
            uint64_t src_index = (y + 16) * display_width + x;
            // コピー先: 現在の画素
            uint64_t dst_index = y * display_width + x;

            buffer[dst_index] = buffer[src_index];
        }
    }

    // 移動した後、一番下の行を背景色でクリアする
    llr_.WriteRect(0, (rows_ - 1) * 16, display_width, 16, bg_color_);

    llr_.Flush();
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