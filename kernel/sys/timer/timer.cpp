#include "timer.hpp"
#include "../../arch/inasm.hpp"

namespace Sys
{
namespace Timer
{

// グローバルティックカウンタ
volatile uint64_t g_system_ticks = 0;

void Tick()
{
    g_system_ticks++;
}

void Sleep(uint64_t ms)
{
    // 終了時刻を計算
    uint64_t target = GetTicksMs() + ms;

    // ビジーウェイト（PAUSEで省電力化）
    while (GetTicksMs() < target)
    {
        PAUSE();
    }
}

} // namespace Timer
} // namespace Sys
