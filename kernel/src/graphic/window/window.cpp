#include "../../../include/std/algorithm.hpp"
#include "../../../include/std/cstring.hpp"
#include "../../pmm.hpp"
#include "window_manager.hpp"
#include "window.hpp"

size_t graphic::Window::_next_id = 1;

graphic::Window::Window(Clip window_clip, const char *title, WindowAttribute attributes) : _attributes(attributes)
{
    _id = _next_id++;
    _window_clip = window_clip;
    strncpy(_title, title, sizeof(_title) - 1);
    _title[sizeof(_title) - 1] = '\0';
    update_client_rect();

    _back_buffer = nullptr;
    if (_client_rect.w > 0 && _client_rect.h > 0)
    {
        const size_t bytes_per_pixel = 4;
        const size_t buffer_size = _client_rect.w * _client_rect.h * bytes_per_pixel;
        const size_t page_size = 4096;
        const uint64_t num_pages = (buffer_size + page_size - 1) / page_size;

        void *raw_buffer = pmm::alloc_pages(num_pages);
        if (raw_buffer != nullptr)
        {
            _back_buffer = reinterpret_cast<uint32_t *>(raw_buffer);
            const size_t buffer_pixels = _client_rect.w * _client_rect.h;
            for (size_t i = 0; i < buffer_pixels; ++i)
            {
                _back_buffer[i] = 0x808080;
            }
        }
    }
}

void graphic::Window::Move(int x, int y)
{
    Clip display_clip = WindowManager::GetInstance().GetScreenClip();
    _window_clip.x = max<int>(0, min<int>(display_clip.w, x));
    _window_clip.y = max<int>(0, min<int>(display_clip.h, y));
    update_client_rect();
}

uint32_t *graphic::Window::GetBackBuffer()
{
    return _back_buffer;
}

void graphic::Window::update_client_rect()
{
    if (HasAttribute(WindowAttribute::NoTitleBar))
    {
        // タイトルバーがない場合 (マウスカーソルなど)
        _client_rect = {_window_clip.x, _window_clip.y, _window_clip.w, _window_clip.h};
    }
    else
    {
        // タイトルバーがある通常のウィンドウ
        const auto client_x = _window_clip.x + kBorderWidth;
        const auto client_y = _window_clip.y + kTitleBarHeight;
        const auto client_w = (_window_clip.w > 2 * kBorderWidth) ? (_window_clip.w - 2 * kBorderWidth) : 0;
        const auto client_h = (_window_clip.h > kTitleBarHeight + kBorderWidth) ? (_window_clip.h - kTitleBarHeight - kBorderWidth) : 0;
        _client_rect = {client_x, client_y, client_w, client_h};
    }
}
