#pragma once

#include <stdint.h>

namespace Sys
{
namespace Timer
{

/**
 * @brief CPUサイクルカウンタを読み取る（高精度計測用）
 *
 * x86_64: RDTSC命令でタイムスタンプカウンタを読み取り
 * AArch64: CNTVCT_EL0レジスタで仮想タイマーカウンタを読み取り
 *
 * @return サイクル数（CPU/タイマー周波数依存）
 */
inline uint64_t ReadCycleCounter()
{
#if defined(__x86_64__)
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;

#elif defined(__aarch64__)
    uint64_t val;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;

#else
    return 0;
#endif
}

/**
 * @brief タイマー周波数を取得する（Hz）
 *
 * x86_64: TSCの周波数は実行時に推定する必要があるため、
 *         ここでは概算値（典型的なCPUで3GHz）を返す
 * AArch64: CNTFRQ_EL0レジスタから正確な周波数を取得可能
 *
 * @return タイマー周波数（Hz）
 */
inline uint64_t GetTimerFrequency()
{
#if defined(__x86_64__)
    // TSCの周波数は動的に変わる可能性があるため、
    // 正確な値が必要な場合はキャリブレーションが必要
    // ここでは概算値として3GHzを返す
    return 3000000000ULL;

#elif defined(__aarch64__)
    // AArch64では正確な周波数をシステムレジスタから取得可能
    uint64_t freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    return freq;

#else
    return 1;
#endif
}

/**
 * @brief サイクルカウンタの差分をナノ秒に変換
 * @param cycles サイクル数の差分
 * @return ナノ秒
 */
inline uint64_t CyclesToNanoseconds(uint64_t cycles)
{
    uint64_t freq = GetTimerFrequency();
    // オーバーフローを避けるため計算順序に注意
    // cycles * 1000000000 / freq の代わりに
    // cycles / (freq / 1000000000) に近い計算を行う
    return (cycles * 1000ULL) / (freq / 1000000ULL);
}

/**
 * @brief サイクルカウンタの差分をマイクロ秒に変換
 * @param cycles サイクル数の差分
 * @return マイクロ秒
 */
inline uint64_t CyclesToMicroseconds(uint64_t cycles)
{
    uint64_t freq = GetTimerFrequency();
    return (cycles * 1000000ULL) / freq;
}

/**
 * @brief サイクルカウンタの差分をミリ秒に変換
 * @param cycles サイクル数の差分
 * @return ミリ秒
 */
inline uint64_t CyclesToMilliseconds(uint64_t cycles)
{
    uint64_t freq = GetTimerFrequency();
    return (cycles * 1000ULL) / freq;
}

/**
 * @brief 高精度タイマークラス（ベンチマーク用）
 *
 * 使用例:
 * @code
 * Sys::Timer::HighPrecisionTimer timer;
 * timer.Start();
 * // 計測したい処理
 * timer.Stop();
 * uint64_t elapsed_us = timer.ElapsedMicroseconds();
 * @endcode
 */
class HighPrecisionTimer
{
public:
    HighPrecisionTimer() : m_start(0), m_end(0) {}

    void Start()
    {
        m_start = ReadCycleCounter();
    }

    void Stop()
    {
        m_end = ReadCycleCounter();
    }

    uint64_t ElapsedCycles() const
    {
        return m_end - m_start;
    }

    uint64_t ElapsedNanoseconds() const
    {
        return CyclesToNanoseconds(ElapsedCycles());
    }

    uint64_t ElapsedMicroseconds() const
    {
        return CyclesToMicroseconds(ElapsedCycles());
    }

    uint64_t ElapsedMilliseconds() const
    {
        return CyclesToMilliseconds(ElapsedCycles());
    }

private:
    uint64_t m_start;
    uint64_t m_end;
};

} // namespace Timer
} // namespace Sys
