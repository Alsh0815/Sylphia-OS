#include "GraphicSystem.hpp"
#include "display/Display.hpp"

namespace Graphic
{

// 静的ストレージを使用してメモリ初期化前でも使用可能に
static DisplayManager s_display_manager;
static Display *s_display = nullptr;

// 静的ストレージ用のバッファ（placement new用）
alignas(Display) static char s_display_storage[sizeof(Display)];

// 静的なLowLayerRenderer
alignas(LowLayerRenderer) static char s_llr_storage[sizeof(LowLayerRenderer)];

DisplayManager *g_display_manager = nullptr;
LowLayerRenderer *g_llr = nullptr;

void InitializeGraphics(const FrameBufferConfig &config)
{
    // 静的なDisplayManagerを使用
    g_display_manager = &s_display_manager;

    // placement newでDisplayを作成（ヒープ不要）
    s_display = new (s_display_storage)
        Display(config.FrameBufferBase, config.FrameBufferSize,
                config.PixelsPerScanLine, config.HorizontalResolution,
                config.VerticalResolution);

    // DisplayManagerにディスプレイを登録
    // STANDARDモードでは直接s_displayを参照するため、AddDisplayはスキップ

    // placement newでLowLayerRendererを作成
    g_llr = new (s_llr_storage) LowLayerRenderer(*g_display_manager);
}

uint64_t GetDisplayWidth()
{
    if (s_display)
        return s_display->GetWidth();
    return 0;
}

uint64_t GetDisplayHeight()
{
    if (s_display)
        return s_display->GetHeight();
    return 0;
}

uint32_t *GetDisplayBuffer()
{
    if (s_display)
        return s_display->GetBuffer();
    return nullptr;
}

void FillScreen(uint32_t color)
{
    if (!s_display)
        return;

    uint32_t *buffer = s_display->GetBuffer();
    uint64_t width = s_display->GetWidth();
    uint64_t height = s_display->GetHeight();

    for (uint64_t y = 0; y < height; ++y)
    {
        for (uint64_t x = 0; x < width; ++x)
        {
            buffer[y * width + x] = color;
        }
    }
}

} // namespace Graphic
