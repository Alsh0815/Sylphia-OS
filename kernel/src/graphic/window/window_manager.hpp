#pragma once
#include "../../../include/framebuffer.hpp"
#include "../../painter.hpp"
#include "../../pmm_vector.hpp"
#include "window.hpp"

namespace graphic
{
    enum WindowFlags : uint8_t
    {
        FLAG_ALWAYS_ON_TOP = 1 << 0
    };

    class WindowContainer
    {
    public:
        WindowContainer() = default;
        Window *window;
        uint8_t flags;
    };

    class WindowManager
    {
    public:
        static WindowManager &GetInstance()
        {
            static WindowManager instance;
            return instance;
        }
        void Init(Framebuffer &fb, Painter &painter);
        Window *CreateWindow(
            Clip clip, const char *title,
            graphic::WindowAttribute attributes = graphic::WindowAttribute::None,
            uint8_t flags = 0b00000000);
        Clip GetScreenClip();
        void MoveWindow(Window *win, int x, int y);
        void Render();

        WindowManager(const WindowManager &) = delete;
        WindowManager &operator=(const WindowManager &) = delete;
        WindowManager(WindowManager &&) = delete;
        WindowManager &operator=(WindowManager &&) = delete;

    private:
        WindowManager() {}
        ~WindowManager() = default;

        void draw_window(Window *win);

        Framebuffer *_framebuffer = nullptr;
        Painter *_painter = nullptr;
        PmmVec<WindowContainer> _windows;
    };
}