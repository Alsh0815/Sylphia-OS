#include "../../../include/std/algorithm.hpp"
#include "window_manager.hpp"

void graphic::WindowManager::Init(Framebuffer &fb, Painter &painter)
{
    _framebuffer = &fb;
    _painter = &painter;
}

graphic::Window *graphic::WindowManager::CreateWindow(Clip clip, const char *title, graphic::WindowAttribute attributes, uint8_t flags)
{
    Window *win = new Window(clip, title, attributes);

    if (win == nullptr)
    {
        return nullptr;
    }

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

Clip graphic::WindowManager::GetScreenClip()
{
    return {0, 0, _framebuffer->width(), _framebuffer->height()};
}

void graphic::WindowManager::MoveWindow(Window *win, int x, int y)
{
    if (win)
    {
        win->Move(x, y);
    }
}

void graphic::WindowManager::draw_window(Window *win)
{
    // === クリップ処理のための準備 ===
    Clip win_clip = win->GetWindowClip();
    Clip client_rect_original = win->GetClientRect(); // ウィンドウ座標系でのクライアント領域
    uint32_t *back_buffer = win->GetBackBuffer();
    if (!back_buffer)
        return;

    // 画面全体の矩形
    const Clip screen_clip = {0, 0, _framebuffer->width(), _framebuffer->height()};

    // === 1. タイトルバーの描画（クリッピング付き） ===
    if (!win->HasAttribute(WindowAttribute::NoTitleBar))
    {
        Clip title_bar_clip = {win_clip.x, win_clip.y, win_clip.w, 30};

        // 画面とタイトルバーの重なっている部分を計算
        int draw_x = max(screen_clip.x, title_bar_clip.x);
        int draw_y = max(screen_clip.y, title_bar_clip.y);
        int end_x = min(screen_clip.x + screen_clip.w, title_bar_clip.x + title_bar_clip.w);
        int end_y = min(screen_clip.y + screen_clip.h, title_bar_clip.y + title_bar_clip.h);

        // 重なっている部分があれば描画
        if (draw_x < end_x && draw_y < end_y)
        {
            _framebuffer->fillRect(draw_x, draw_y, end_x - draw_x, end_y - draw_y, {100, 100, 120});
            _painter->setColor({255, 255, 255});
            _painter->drawText(win_clip.x + 5, win_clip.y + 8, win->GetTitle());
        }
    }

    // === 2. クライアント領域の描画（クリッピング付き） ===
    // 画面とクライアント領域の重なっている部分(描画先)を計算
    int draw_start_x = max(screen_clip.x, client_rect_original.x);
    int draw_start_y = max(screen_clip.y, client_rect_original.y);
    int draw_end_x = min(screen_clip.x + screen_clip.w, client_rect_original.x + client_rect_original.w);
    int draw_end_y = min(screen_clip.y + screen_clip.h, client_rect_original.y + client_rect_original.h);

    // 重なりがなければ描画しない
    if (draw_start_x >= draw_end_x || draw_start_y >= draw_end_y)
    {
        return;
    }

    // バックバッファのどこから読み取り始めるかを計算
    int back_buffer_start_x = draw_start_x - client_rect_original.x;
    int back_buffer_start_y = draw_start_y - client_rect_original.y;

    for (int y = 0; y < (draw_end_y - draw_start_y); ++y)
    {
        for (int x = 0; x < (draw_end_x - draw_start_x); ++x)
        {
            // バックバッファから対応するピクセルを読み出す
            uint32_t pixel = back_buffer[(back_buffer_start_y + y) * client_rect_original.w + (back_buffer_start_x + x)];

            if (win->HasAttribute(WindowAttribute::Transparent) && pixel == 0xFFFF00FF)
            {
                continue;
            }

            // 画面の正しい位置にピクセルを書き込む
            _framebuffer->putPixel(draw_start_x + x, draw_start_y + y, pixel);
        }
    }
}

void graphic::WindowManager::Render()
{
    if (!_framebuffer || !_painter)
        return;
    _framebuffer->clear({50, 60, 80});

    // 通常のウィンドウを描画
    for (auto &container : _windows)
    {
        if ((container.flags & FLAG_ALWAYS_ON_TOP) == 0)
        {
            draw_window(container.window);
        }
    }

    // 最前面表示のウィンドウを描画
    for (auto &container : _windows)
    {
        if ((container.flags & FLAG_ALWAYS_ON_TOP) != 0)
        {
            draw_window(container.window);
        }
    }
}