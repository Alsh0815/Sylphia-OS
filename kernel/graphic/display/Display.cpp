#include "Display.hpp"
#include "cxx.hpp"
#include "memory/memory_manager.hpp"

namespace Graphic
{
Display::Display(const uint64_t frame_buffer_base,
                 const uint64_t frame_buffer_size,
                 const uint64_t pixels_per_scan_line,
                 const uint64_t display_width, const uint64_t display_height)
    : _frame_buffer_base(frame_buffer_base),
      _frame_buffer_size(frame_buffer_size), _display_width(display_width),
      _display_height(display_height),
      _pixels_per_scan_line(pixels_per_scan_line)
{
    _front_buffer = reinterpret_cast<uint32_t *>(_frame_buffer_base);
    _current_buffer = _front_buffer;
    _drawing_buf_index = 0;
    _display_buf_index = 0;
    _buffer_count = 0;
    _back_buffer[0] = nullptr;
    _back_buffer[1] = nullptr;
    _back_buffer[2] = nullptr;
}

void Display::AllocateBackBuffers(RenderMode mode)
{
    if (_back_buffer[0] != nullptr)
        return;

    size_t buffer_bytes = _frame_buffer_size;

    if (mode == RenderMode::DOUBLE_BUFFER)
    {
        void *buf = MemoryManager::Allocate(buffer_bytes, 4096);
        if (!buf)
        {
            return;
        }
        _back_buffer[0] = static_cast<uint32_t *>(buf);
        _buffer_count = 1;

        memcpy(_back_buffer[0], _front_buffer, _frame_buffer_size);
    }
    else if (mode == RenderMode::TRIPLE_BUFFER)
    {
        void *buf0 = MemoryManager::Allocate(buffer_bytes, 4096);
        void *buf1 = MemoryManager::Allocate(buffer_bytes, 4096);
        if (!buf0 || !buf1)
        {
            if (buf0)
                MemoryManager::Free(buf0, buffer_bytes);
            if (buf1)
                MemoryManager::Free(buf1, buffer_bytes);
            return;
        }
        _back_buffer[0] = static_cast<uint32_t *>(buf0);
        _back_buffer[1] = static_cast<uint32_t *>(buf1);
        _buffer_count = 2;

        memcpy(_back_buffer[0], _front_buffer, _frame_buffer_size);
        memcpy(_back_buffer[1], _front_buffer, _frame_buffer_size);
    }
}

void Display::Flip()
{
    if (_render_mode == RenderMode::STANDARD)
        return;

    if (_render_mode == RenderMode::DOUBLE_BUFFER)
        return;

    _display_buf_index = _drawing_buf_index;
    _drawing_buf_index = (_drawing_buf_index + 1) % _buffer_count;
    _current_buffer = _back_buffer[_drawing_buf_index];
}

void Display::Flush()
{
    if (_render_mode == RenderMode::STANDARD)
        return;

    if (_render_mode == RenderMode::DOUBLE_BUFFER)
    {
        memcpy(_front_buffer, _back_buffer[0], _frame_buffer_size);
    }
    else if (_render_mode == RenderMode::TRIPLE_BUFFER)
    {
        memcpy(_front_buffer, _back_buffer[_display_buf_index],
               _frame_buffer_size);
    }
}

void Display::SetRenderMode(RenderMode mode)
{
    if (mode == RenderMode::STANDARD)
    {
        _render_mode = mode;
        _current_buffer = _front_buffer;
    }
    else if (_back_buffer[0] != nullptr)
    {
        _render_mode = mode;
        _drawing_buf_index = 0;
        _current_buffer = _back_buffer[0];
    }
}
} // namespace Graphic