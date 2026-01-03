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
     * Add display (raw pointer, not owned) to display manager.
     * Use for static/placement-new displays that shouldn't be deleted.
     *
     * @param display Display pointer to add (ownership NOT transferred)
     * @return Result<uint32_t display_id, Error> Result of add display
     */
    Result<uint32_t, Error> AddDisplayRaw(Display *display);
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
    /*
     * Get the number of displays.
     * @return Number of displays
     */
    size_t GetDisplayCount() const
    {
        return _displays.Size() + _raw_display_count;
    }
    /*
     * Get display by ID.
     * @param display_id Display ID
     * @return Pointer to Display, or nullptr if not found
     */
    Display *GetDisplay(uint32_t display_id)
    {
        // まず生ポインタ配列を確認
        if (display_id < _raw_display_count)
            return _raw_displays[display_id];
        // 次にUniquePtr配列を確認
        uint32_t unique_id = display_id - _raw_display_count;
        if (unique_id >= _displays.Size())
            return nullptr;
        return _displays[unique_id].Get();
    }

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
    static const size_t MAX_DISPLAYS = 16;
    Vector<UniquePtr<Display>> _displays;
    Display *_raw_displays[MAX_DISPLAYS] = {nullptr};
    uint32_t _raw_display_count = 0;
    uint32_t _active_display = 0;
    volatile int _lock_flag = 0;
    bool _interrupts_enabled = false;
};
} // namespace Graphic