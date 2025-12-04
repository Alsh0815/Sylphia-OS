#pragma once
#include "graphics.hpp"

class Console
{
  public:
    // コンストラクタ: 画面設定と色を受け取る
    Console(const FrameBufferConfig &config, uint32_t fg_color,
            uint32_t bg_color);

    // 文字列を出力する
    void PutString(const char *s);

    // 指定した色に変更する（エラーログなどで赤くしたい時用）
    void SetColor(uint32_t fg_color, uint32_t bg_color);

    void Panic(uint32_t fg_color, uint32_t bg_color);

  private:
    FrameBufferConfig config_;
    uint32_t fg_color_, bg_color_;
    int cursor_row_,
        cursor_column_;  // 現在の文字位置 (ピクセルではなく文字単位)
    int rows_, columns_; // 画面全体で何行・何文字入るか

    // 1行スクロールなどの内部処理
    void NewLine();
    void Refresh(); // スクロール処理の実体

    // エスケープシーケンス解析用
    enum State
    {
        kNormal,
        kEsc,
        kBracket,
        kParam
    } state_ = kNormal;
    char esc_buf_[32];
    int esc_buf_idx_ = 0;
    uint32_t default_fg_color_, default_bg_color_;

    void ProcessEscapeSequence(char c);
    void ExecuteAnsiCommand();
};

extern Console *g_console;