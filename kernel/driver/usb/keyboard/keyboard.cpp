#include "driver/usb/keyboard/keyboard.hpp"
#include "app/elf/elf_loader.hpp"
#include "cxx.hpp"
#include "driver/usb/mass_storage/mass_storage.hpp"
#include "memory/memory_manager.hpp"
#include "printk.hpp"
#include "sys/std/file_descriptor.hpp"

USB::Keyboard *g_usb_keyboard = nullptr;

namespace USB
{
const char Keyboard::kHidToAsciiMap[256] = {
    0,   0,    0,   0,   'a',  'b',  'c',  'd',  'e', 'f', 'g', 'h',
    'i', 'j',  'k', 'l', 'm',  'n',  'o',  'p',  'q', 'r', 's', 't',
    'u', 'v',  'w', 'x', 'y',  'z',  '1',  '2',  '3', '4', '5', '6',
    '7', '8',  '9', '0', '\n', 0x1B, 0x08, 0x09, ' ', '-', '=', '[',
    ']', '\\', '#', ';', '\'', '`',  ',',  '.',  '/',
    // ... 残りは必要に応じて埋める ...
};

const char Keyboard::kHidToAsciiMapShift[256] = {
    0,   0,   0,   0,   'A',  'B',  'C',  'D',  'E', 'F', 'G', 'H',
    'I', 'J', 'K', 'L', 'M',  'N',  'O',  'P',  'Q', 'R', 'S', 'T',
    'U', 'V', 'W', 'X', 'Y',  'Z',  '!',  '@',  '#', '$', '%', '^',
    '&', '*', '(', ')', '\n', 0x1B, 0x08, 0x09, ' ', '_', '+', '{',
    '}', '|', '~', ':', '"',  '~',  '<',  '>',  '?',
    // ...
};

Keyboard::Keyboard(XHCI::Controller *controller, uint8_t slot_id)
    : controller_(controller), slot_id_(slot_id), ep_interrupt_in_(0)
{
}

bool Keyboard::Initialize()
{
    DeviceDescriptor dev_desc;
    if (!controller_->ControlIn(slot_id_,
                                0x80,   // ReqType: Device, In
                                6,      // Request: GET_DESCRIPTOR
                                0x0100, // Value: Device(1), Index 0
                                0,      // Index
                                18,     // Length
                                &dev_desc))
    {
        return false;
    }

    uint8_t *buf = static_cast<uint8_t *>(MemoryManager::Allocate(256, 64));
    ConfigurationDescriptor *cd =
        reinterpret_cast<ConfigurationDescriptor *>(buf);

    if (!controller_->ControlIn(slot_id_, 0x80, 6, 0x0200, 0, 9, buf))
    {
        MemoryManager::Free(buf, 256);
        return false;
    }

    uint16_t total_length = cd->total_length;

    if (!controller_->ControlIn(slot_id_, 0x80, 6, 0x0200, 0, total_length,
                                buf))
    {
        MemoryManager::Free(buf, 256);
        return false;
    }

    uint8_t *p = buf;
    uint8_t *end = buf + total_length;
    bool is_keyboard = false;

    while (p < end)
    {
        uint8_t len = p[0];
        uint8_t type = p[1];

        if (type == 4) // Interface
        {
            InterfaceDescriptor *id =
                reinterpret_cast<InterfaceDescriptor *>(p);
            if (id->interface_class == 3 && id->interface_sub_class == 1 &&
                id->interface_protocol == 1)
            {
                is_keyboard = true;
            }
            else
            {
                is_keyboard = false;
            }
        }
        else if (type == 5) // Endpoint
        {
            if (is_keyboard)
            {
                EndpointDescriptor *ed =
                    reinterpret_cast<EndpointDescriptor *>(p);
                // Interrupt(3) かつ IN(0x80)
                if ((ed->endpoint_address & 0x80) &&
                    (ed->attributes & 0x03) == 3)
                {
                    ep_interrupt_in_ = ed->endpoint_address;

                    uint16_t max_pkt = 8; // Boot Protocol Keyboardは大抵8バイト
                    uint8_t interval = 10; // 10ms

                    if (!controller_->ConfigureEndpoint(
                            slot_id_, ep_interrupt_in_, max_pkt, interval, 3))
                    {
                        return false;
                    }

                    memset(buf_, 0, 8);
                    memset(prev_buf_, 0, 8);
                    controller_->SendNormalTRB(slot_id_, ep_interrupt_in_, buf_,
                                               8);

                    return true;
                }
            }
        }
        p += len;
    }

    MemoryManager::Free(buf, 256);
    return (ep_interrupt_in_ != 0);
}

void Keyboard::ForceSendTRB()
{
    memcpy(prev_buf_, buf_, 8);
    memset(buf_, 0, 8);
    controller_->SendNormalTRB(slot_id_, ep_interrupt_in_, buf_, 8);
}

void Keyboard::Update()
{
    // Event Ring競合回避: MSCがビジーの場合はポーリングをスキップ
    // これにより、MSC向けのイベントを誤って消費することを防ぐ
    if (g_mass_storage && g_mass_storage->IsBusy())
    {
        return;
    }

    int result = controller_->PollEndpoint(slot_id_, ep_interrupt_in_);

    if (result == 1)
    {
        bool shift = (buf_[0] & 0x02) || (buf_[0] & 0x20);

        for (int i = 2; i < 8; ++i)
        {
            uint8_t key = buf_[i];
            if (key == 0)
                continue;
            bool was_pressed = false;
            for (int j = 2; j < 8; ++j)
            {
                if (prev_buf_[j] == key)
                {
                    was_pressed = true;
                    break;
                }
            }

            if (!was_pressed)
            {
                char ascii = 0;
                if (shift)
                    ascii = kHidToAsciiMapShift[key];
                else
                    ascii = kHidToAsciiMap[key];

                if (ascii != 0)
                {
                    // キーボード入力はKeyboardFD経由で配送
                    // シェル等のアプリはRead()で受け取る
                    if (g_fds[0] && g_fds[0]->GetType() == FDType::FD_KEYBOARD)
                    {
                        ((KeyboardFD *)g_fds[0])->OnInput(ascii);
                    }
                }
            }
        }

        memcpy(prev_buf_, buf_, 8);
        memset(buf_, 0, 8);
        controller_->SendNormalTRB(slot_id_, ep_interrupt_in_, buf_, 8);
    }
}
} // namespace USB