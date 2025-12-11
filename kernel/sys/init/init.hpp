#pragma once

#include "memory/memory.hpp"

namespace Sys
{
namespace Init
{

// カーネルコア初期化
// セグメント、割り込み、SSE、メモリマネージャ、カーネルスタック、ページング、システムコールを初期化
void InitializeCore(const MemoryMap &memmap);

// 標準I/Oとロガーの初期化
void InitializeIO();

} // namespace Init
} // namespace Sys
