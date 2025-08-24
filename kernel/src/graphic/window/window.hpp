#pragma once
#include <cstddef>
#include <cstdint>
#include "../../../include/framebuffer.hpp"

namespace graphic
{
    enum class WindowAttribute
    {
        None = 0,
        NoTitleBar = 1 << 0,  // タイトルバーを描画しない
        Transparent = 1 << 1, // 背景を透過する
    };

    inline WindowAttribute operator|(WindowAttribute a, WindowAttribute b)
    {
        return static_cast<WindowAttribute>(static_cast<int>(a) | static_cast<int>(b));
    }
    inline bool operator&(WindowAttribute a, WindowAttribute b)
    {
        return static_cast<int>(a) & static_cast<int>(b);
    }

    class Window
    {
    public:
        Window(Clip window_clip, const char *title, WindowAttribute attributes = WindowAttribute::None);

        uint32_t *GetBackBuffer();
        char *GetTitle() { return _title; }
        Clip GetClientRect() { return _client_rect; }
        Clip GetWindowClip() { return _window_clip; }
        bool HasAttribute(WindowAttribute attr) const { return _attributes & attr; }
        void Move(int x, int y);

    private:
        void update_client_rect();

        Clip _window_clip;
        Clip _client_rect;
        uint32_t *_back_buffer;

        char _title[256];

        size_t _id;
        bool _is_active = false;
        WindowAttribute _attributes;

        static constexpr uint32_t kTitleBarHeight = 30;
        static constexpr uint32_t kBorderWidth = 4;

        static size_t _next_id;
    };
}