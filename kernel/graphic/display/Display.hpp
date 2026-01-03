#pragma once

#include "cxx.hpp"
#include "new.hpp"
#include <stdint.h>

namespace Graphic
{
enum class RenderMode : uint8_t
{
    STANDARD = 1,
    DOUBLE_BUFFER = 2,
    TRIPLE_BUFFER = 3
};
class Display
{
public:
    Display(const uint64_t frame_buffer_base, const uint64_t frame_buffer_size,
            const uint64_t pixels_per_scan_line, const uint64_t display_width,
            const uint64_t display_height);
    ~Display()
    {
        if (_back_buffer[0])
            delete[] _back_buffer[0];
        if (_back_buffer[1])
            delete[] _back_buffer[1];
    }

    void Flip();
    /*
     * Flush display.
     */
    void Flush();
    /*
     * Allocate back buffers for double/triple buffering.
     * Called after memory manager is initialized.
     */
    void AllocateBackBuffers();
    uint64_t GetWidth()
    {
        return _display_width;
    };
    uint64_t GetHeight()
    {
        return _display_height;
    };
    /*
     * Get display buffer.
     *
     * @return uint32_t * buffer
     */
    uint32_t *GetBuffer()
    {
        return _current_buffer;
    }

private:
    const uint64_t _frame_buffer_base;
    const uint64_t _frame_buffer_size;
    const uint64_t _display_width;
    const uint64_t _display_height;
    const uint64_t _pixels_per_scan_line;
    uint32_t *_front_buffer;
    uint32_t *_back_buffer[2];
    uint32_t *_current_buffer;
    int _drawing_buf_index = 0;
    RenderMode _render_mode = RenderMode::STANDARD;
};
} // namespace Graphic