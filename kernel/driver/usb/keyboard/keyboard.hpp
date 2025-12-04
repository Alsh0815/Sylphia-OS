#pragma once
#include "driver/usb/usb.hpp"
#include "driver/usb/xhci.hpp"

namespace USB
{
void UsbKeyboardOnInterrupt();
class Keyboard
{
  public:
    Keyboard(XHCI::Controller *controller, uint8_t slot_id);
    bool Initialize();
    void Update();      // ポーリング用（互換性のため残す）
    void OnInterrupt(); // 割り込みハンドラから呼ばれる
    void ForceSendTRB();

  private:
    void ProcessKeyboardData(); // キー入力処理ロジック

    XHCI::Controller *controller_;
    uint8_t slot_id_;
    uint8_t ep_interrupt_in_;
    uint8_t buf_[8];
    uint8_t prev_buf_[8];

    static const char kHidToAsciiMap[256];
    static const char kHidToAsciiMapShift[256];
};
} // namespace USB

extern USB::Keyboard *g_usb_keyboard;