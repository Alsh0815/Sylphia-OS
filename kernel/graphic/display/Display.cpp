#include "Display.hpp"

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
    // STANDARDモードではバックバッファを使用しないため、
    // 割り当てはAllocateBackBuffers()で明示的に行う
    _back_buffer[0] = nullptr;
    _back_buffer[1] = nullptr;
}

void Display::AllocateBackBuffers()
{
    if (_back_buffer[0] != nullptr)
        return; // 既に割り当て済み

    size_t buffer_size = _frame_buffer_size / sizeof(uint32_t);
    _back_buffer[0] = new uint32_t[buffer_size];
    _back_buffer[1] = new uint32_t[buffer_size];
}

void Display::Flip()
{
    if (_render_mode == RenderMode::STANDARD)
        return;
    if (_drawing_buf_index == 0)
    {
        _drawing_buf_index = 1;
    }
    else
    {
        _drawing_buf_index = 0;
    }
    _current_buffer = _back_buffer[_drawing_buf_index];
}

void Display::Flush()
{
    if (_render_mode == RenderMode::STANDARD)
        return;
    memcpy(_front_buffer, _current_buffer, _frame_buffer_size);
}
} // namespace Graphic