#pragma once
#include <cstddef>
#include <cstdint>
#include "../../../include/framebuffer.hpp"

namespace graphic
{
    class Window
    {
    public:
        Window(Clip window_clip, const char *title);

        uint32_t *GetBackBuffer();
        char *GetTitle() { return _title; }
        Clip GetClientRect() { return _client_rect; }
        Clip GetWindowClip() { return _window_clip; }

    private:
        void update_client_rect();

        Clip _window_clip;
        Clip _client_rect;
        uint32_t *_back_buffer;

        char _title[256];

        size_t _id;
        bool _is_active = false;

        static constexpr uint32_t kTitleBarHeight = 30;
        static constexpr uint32_t kBorderWidth = 4;

        static size_t _next_id;
    };
}