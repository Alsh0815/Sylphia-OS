#include <stdint.h>

struct FrameBufferConfig
{
    uint64_t FrameBufferBase;
    uint64_t FrameBufferSize;
    uint32_t HorizontalResolution;
    uint32_t VerticalResolution;
    uint32_t PixelsPerScanLine;
};

extern "C" __attribute__((ms_abi)) void KernelMain(const FrameBufferConfig &config)
{

    // 画面操作用のポインタ
    uint32_t *frame_buffer = reinterpret_cast<uint32_t *>(config.FrameBufferBase);

    // 画面の中央あたりに 白い四角 (100x100) を描画
    // 白 = 0xFFFFFFFF
    for (uint32_t y = 0; y < 100; ++y)
    {
        for (uint32_t x = 0; x < 100; ++x)
        {
            uint32_t index = (config.VerticalResolution / 2 + y) * config.PixelsPerScanLine + (config.HorizontalResolution / 2 + x);
            frame_buffer[index] = 0xFFFFFFFF;
        }
    }

    while (1)
    {
        __asm__ volatile("hlt");
    }
}