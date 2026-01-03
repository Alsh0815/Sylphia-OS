#pragma once

#include "Display.hpp"
#include "Error.hpp"
#include "arch/inasm.hpp"
#include "util/UniquePtr.hpp"
#include "util/Vector.hpp"
#include <stdint.h>

namespace Graphic
{
class DisplayManager
{
public:
    enum class Error : uint8_t
    {
        DisplayManagerFull,
        DisplayNotFound,
        Unknown,
    };

    /*
     * Add display to display manager.
     *
     * @param display Display to add
     * @return Result<uint32_t display_id, Error> Result of add display
     */
    Result<uint32_t, Error> AddDisplay(UniquePtr<Display> display);
    /*
     * Flush active display.
     *
     * @return Result<uint32_t display_id, Error> Result of flush display
     */
    bool Flush();
    /*
     * Flush all displays.
     */
    void FlushAll();
    /*
     * Get active display buffer.
     *
     * @return uint32_t * buffer
     */
    uint32_t *GetBuffer();
    uint64_t GetWidth();
    uint64_t GetHeight();
    /*
     * Set active display.
     *
     * @param display_id Display id to set
     * @return Result<uint32_t display_id, Error> Result of set active display
     */
    Result<uint32_t, Error> SetActiveDisplay(uint32_t display_id);

    void Lock()
    {
        while (__atomic_test_and_set(&_lock_flag, __ATOMIC_ACQUIRE))
        {
            PAUSE();
        }
    }

    void Unlock()
    {
        __atomic_clear(&_lock_flag, __ATOMIC_RELEASE);
    }

private:
    Vector<UniquePtr<Display>> _displays;
    uint32_t _active_display = 0;
    static const size_t MAX_DISPLAYS = 16;
    volatile int _lock_flag = 0;
    bool _interrupts_enabled = false;
};
} // namespace Graphic