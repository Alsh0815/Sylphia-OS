#include "sys/std/file_descriptor.hpp"
#include "driver/usb/keyboard/keyboard.hpp"

extern USB::Keyboard *g_usb_keyboard;

int KeyboardFD::Read(void *buf, size_t len)
{
    if (g_usb_keyboard)
    {
        g_usb_keyboard->Update();
    }

    if (count_ == 0)
    {
        return 0;
    }

    char *p = static_cast<char *>(buf);
    size_t read_count = 0;

    while (read_count < len && count_ > 0)
    {
        *p++ = buffer_[read_pos_];
        read_pos_ = (read_pos_ + 1) % kBufferSize;
        count_--;
        read_count++;
    }
    return read_count;
}
