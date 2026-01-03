#pragma once

#include "display/DisplayManager.hpp"
#include "llr/LowLayerRenderer.hpp"

struct FrameBufferConfig
{
    uint64_t FrameBufferBase;
    uint64_t FrameBufferSize;
    uint32_t HorizontalResolution;
    uint32_t VerticalResolution;
    uint32_t PixelsPerScanLine;
    uint64_t EcamBaseAddress; // AArch64 ECAM用
    uint8_t EcamStartBus;
    uint8_t EcamEndBus;
    uint8_t EcamPadding[6]; // アライメント用
};

namespace Graphic
{
/**
 * グラフィックシステムのグローバルインスタンス
 */
extern DisplayManager *g_display_manager;
extern LowLayerRenderer *g_llr;

/**
 * グラフィックシステムを初期化する
 *
 * @param config フレームバッファ設定（ブートローダーから渡される）
 */
void InitializeGraphics(const FrameBufferConfig &config);

/**
 * アクティブディスプレイの幅を取得
 */
uint64_t GetDisplayWidth();

/**
 * アクティブディスプレイの高さを取得
 */
uint64_t GetDisplayHeight();

/**
 * アクティブディスプレイのバッファを取得
 */
uint32_t *GetDisplayBuffer();

/**
 * 画面全体を指定色で塗りつぶす
 * InitializeGraphics後、メモリ初期化前でも使用可能
 */
void FillScreen(uint32_t color);

} // namespace Graphic
