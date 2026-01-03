#include "LowLayerRenderer.hpp"
#include "cxx.hpp"
#include "graphic/GraphicSystem.hpp"
#include <std/algorithm.hpp>
#include <std/utility.hpp>

class DisplayManagerLock
{
public:
    DisplayManagerLock(Graphic::DisplayManager &manager) : manager_(manager)
    {
        manager_.Lock();
    }
    ~DisplayManagerLock()
    {
        manager_.Unlock();
    }

private:
    Graphic::DisplayManager &manager_;
};

namespace Graphic
{
bool LowLayerRenderer::Flush()
{
    return _display_manager.Flush();
}

bool LowLayerRenderer::WriteBitmap(uint64_t x, uint64_t y, uint64_t width,
                                   uint64_t height, const uint32_t *bitmap)
{
    const uint64_t display_w = GetDisplayWidth();
    const uint64_t display_h = GetDisplayHeight();
    if (x >= display_w || y >= display_h)
        return false;
    uint32_t *get_buf = GetDisplayBuffer();
    if (get_buf == nullptr)
        return false;
    for (uint64_t i = 0; i < height; i++)
    {
        if (y + i >= display_h)
            break;
        uint32_t *row_ptr = &get_buf[(y + i) * display_w + x];
        const uint32_t *src_row = &bitmap[i * width];
        memcpy(row_ptr, src_row,
               std::min(width, display_w - x) * sizeof(uint32_t));
    }
    return true;
}

bool LowLayerRenderer::WriteBitmapAtomic(uint64_t x, uint64_t y, uint64_t width,
                                         uint64_t height,
                                         const uint32_t *bitmap)
{
    DisplayManagerLock lock(_display_manager);
    return WriteBitmap(x, y, width, height, bitmap);
}

bool LowLayerRenderer::WritePixel(uint64_t x, uint64_t y, uint32_t color)
{
    const uint64_t display_w = GetDisplayWidth();
    const uint64_t display_h = GetDisplayHeight();
    if (x >= display_w || y >= display_h)
        return false;
    uint32_t *get_buf = GetDisplayBuffer();
    if (get_buf == nullptr)
        return false;
    get_buf[y * display_w + x] = color;
    return true;
}

bool LowLayerRenderer::WriteRect(uint64_t x, uint64_t y, uint64_t width,
                                 uint64_t height, uint32_t color)
{
    const uint64_t display_w = GetDisplayWidth();
    const uint64_t display_h = GetDisplayHeight();
    if (x >= display_w || y >= display_h)
        return false;
    uint32_t *get_buf = GetDisplayBuffer();
    if (get_buf == nullptr)
        return false;
    for (uint64_t i = 0; i < height; i++)
    {
        if (y + i >= display_h)
            break;
        uint32_t *row_ptr = &get_buf[(y + i) * display_w + x];
        for (uint64_t j = 0; j < width; j++)
        {
            if (x + j >= display_w)
                break;
            row_ptr[j] = color;
        }
    }
    return true;
}

bool LowLayerRenderer::WriteRectAtomic(uint64_t x, uint64_t y, uint64_t width,
                                       uint64_t height, uint32_t color)
{
    DisplayManagerLock lock(_display_manager);
    return WriteRect(x, y, width, height, color);
}
} // namespace Graphic