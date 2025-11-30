#pragma once
#include <stdint.h>

// キーボード配列の種類
enum class KeyboardLayout
{
    US_Standard,
    JP_Standard
};

// スキャンコードをASCII文字に変換する関数
// scancode: キーボードから来た生データ
// shift: Shiftキーが押されているか
// layout: 配列の種類
char ConvertScanCodeToAscii(uint8_t scancode, bool shift, KeyboardLayout layout);