#pragma once
#include <stdint.h>

namespace PCI
{

    // PCIコンフィギュレーション空間のI/Oポート
    const uint16_t kConfigAddress = 0x0CF8;
    const uint16_t kConfigData = 0x0CFC;

    // デバイス情報を保持する構造体
    struct Device
    {
        uint8_t bus;
        uint8_t device;
        uint8_t function;
        uint16_t vendor_id;
        uint16_t device_id;
        uint8_t base_class;
        uint8_t sub_class;
        uint8_t prog_if; // Programming Interface
    };

    uintptr_t ReadBar0(const Device &dev);
    uint32_t ReadConfReg(const Device &dev, uint8_t reg_addr);

    // バス全体をスキャンして、見つかったデバイスを画面に表示する
    void ScanAllBus();

} // namespace PCI