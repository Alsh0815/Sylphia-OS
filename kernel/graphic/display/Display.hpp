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
        for (int i = 0; i < 3; ++i)
        {
            if (_back_buffer[i])
                delete[] _back_buffer[i];
        }
    }

    void Flip();
    /*
     * Flush display.
     * Copies the current back buffer to the front buffer (display).
     */
    void Flush();
    /*
     * Allocate back buffers for double/triple buffering.
     * Called after memory manager is initialized.
     * @param mode The render mode to use (DOUBLE_BUFFER or TRIPLE_BUFFER)
     */
    void AllocateBackBuffers(RenderMode mode);
    /*
     * Get the current render mode.
     * @return Current RenderMode
     */
    RenderMode GetRenderMode() const
    {
        return _render_mode;
    }
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
    /*
     * Set the render mode.
     * @param mode The render mode (STANDARD, DOUBLE_BUFFER, TRIPLE_BUFFER)
     */
    void SetRenderMode(RenderMode mode);

private:
    const uint64_t _frame_buffer_base;
    const uint64_t _frame_buffer_size;
    const uint64_t _display_width;
    const uint64_t _display_height;
    const uint64_t _pixels_per_scan_line;
    uint32_t *_front_buffer;
    uint32_t *_back_buffer[3]; // 最大3バッファ（トリプルバッファ用）
    uint32_t *_current_buffer;
    uint8_t _drawing_buf_index = 0; // 現在描画中のバックバッファインデックス
    uint8_t _display_buf_index = 0; // 表示用に準備されたバッファインデックス
    uint8_t _buffer_count = 0;      // 使用するバックバッファ数
    RenderMode _render_mode = RenderMode::STANDARD;
};
} // namespace Graphic