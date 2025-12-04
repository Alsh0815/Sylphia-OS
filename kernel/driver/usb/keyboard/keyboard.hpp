#pragma once
#include "driver/usb/usb.hpp"
#include "driver/usb/xhci.hpp"

namespace USB
{
class Keyboard
{
  public:
    Keyboard(XHCI::Controller *controller, uint8_t slot_id);
    bool Initialize();

    void Update();

  private:
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