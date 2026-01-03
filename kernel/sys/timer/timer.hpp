#pragma once

#include <stdint.h>

namespace Sys
{
namespace Timer
{

// タイマー割り込みの間隔（ミリ秒）
constexpr uint64_t kTickIntervalMs = 10;

// システム起動からのティック数（volatile: 割り込みハンドラから更新されるため）
extern volatile uint64_t g_system_ticks;

/**
 * @brief ティックカウンタをインクリメントする
 * @note TimerHandlerから呼び出される。直接呼び出さないこと。
 */
void Tick();

/**
 * @brief システム起動からのティック数を取得
 * @return ティック数（1ティック = 10ms）
 */
inline uint64_t GetTicks()
{
    return g_system_ticks;
}

/**
 * @brief システム起動からの経過時間をミリ秒で取得
 * @return ミリ秒
 */
inline uint64_t GetTicksMs()
{
    return g_system_ticks * kTickIntervalMs;
}

/**
 * @brief システム起動からの経過時間を秒で取得
 * @return 秒
 */
inline uint64_t GetTicksSec()
{
    return GetTicksMs() / 1000;
}

/**
 * @brief 指定時間だけ待機する（ビジーウェイト）
 * @param ms 待機時間（ミリ秒）
 * @note 割り込みが有効でないと動作しない
 */
void Sleep(uint64_t ms);

/**
 * @brief 指定時間だけ待機する（ビジーウェイト）
 * @param sec 待機時間（秒）
 */
inline void SleepSec(uint64_t sec)
{
    Sleep(sec * 1000);
}

} // namespace Timer
} // namespace Sys
