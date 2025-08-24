#include "window_manager.hpp"

void graphic::WindowManager::Init(Framebuffer &fb, Painter &painter)
{
    _framebuffer = &fb;
    _painter = &painter;
}

graphic::Window *graphic::WindowManager::CreateWindow(Clip clip, const char *title, uint8_t flags)
{
    Window *win = new Window(clip, title);

    if (win->GetBackBuffer() == nullptr)
    {
        delete win;
        return nullptr;
    }

    WindowContainer container;
    container.window = win;
    container.flags = flags;

    _windows.push_back(container);
    return win;
}

void graphic::WindowManager::Render()
{
    if (!_framebuffer || !_painter)
        return;

    _framebuffer->clear({50, 60, 80});

    for (auto &container : _windows)
    {
        Window *win = container.window;
        Clip win_clip = win->GetWindowClip();
        _framebuffer->fillRect(win_clip.x, win_clip.y,
                               win_clip.w, 30, {100, 100, 120});
        _painter->setColor({255, 255, 255});
        _painter->drawText(win_clip.x + 5, win_clip.y + 8, win->GetTitle());
        uint32_t *back_buffer = win->GetBackBuffer();
        Clip client_rect = win->GetClientRect();

        for (uint32_t y = 0; y < client_rect.h; ++y)
        {
            for (uint32_t x = 0; x < client_rect.w; ++x)
            {
                uint32_t color = back_buffer[y * client_rect.w + x];
                _framebuffer->putPixel(client_rect.x + x, client_rect.y + y, color);
            }
        }
    }
}