#pragma once

#include "graphic/display/DisplayManager.hpp"
#include <stdint.h>

namespace Graphic
{
class LowLayerRenderer
{
public:
    LowLayerRenderer(DisplayManager &display_manager)
        : _display_manager(display_manager)
    {
    }
    ~LowLayerRenderer();

    /*
     * Flush buffer to display.
     */
    bool Flush();
    /*
     * Write bitmap to buffer.
     */
    bool WriteBitmap(uint64_t x, uint64_t y, uint64_t width, uint64_t height,
                     const uint32_t *bitmap);
    /*
     * Write bitmap to buffer atomically.
     */
    bool WriteBitmapAtomic(uint64_t x, uint64_t y, uint64_t width,
                           uint64_t height, const uint32_t *bitmap);
    /*
     * Write pixel to buffer.
     */
    bool WritePixel(uint64_t x, uint64_t y, uint32_t color);
    /*
     * Write rectangle to buffer.
     */
    bool WriteRect(uint64_t x, uint64_t y, uint64_t width, uint64_t height,
                   uint32_t color);
    /*
     * Write rectangle to buffer atomically.
     */
    bool WriteRectAtomic(uint64_t x, uint64_t y, uint64_t width,
                         uint64_t height, uint32_t color);

private:
    DisplayManager &_display_manager;
};
} // namespace Graphic