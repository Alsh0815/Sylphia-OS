#pragma once
#include <stdint.h>

namespace PCI
{

#if defined(__x86_64__)
// PCIコンフィギュレーション空間のI/Oポート (x86_64のみ)
const uint16_t kConfigAddress = 0x0CF8;
const uint16_t kConfigData = 0x0CFC;
#endif

// AArch64用: グローバルECAMベースアドレス
extern uintptr_t g_ecam_base;
extern uint8_t g_ecam_start_bus;
extern uint8_t g_ecam_end_bus;

// ECAM初期化関数
void InitializePCI(uintptr_t ecam_base, uint8_t start_bus, uint8_t end_bus);

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
void WriteConfReg(const Device &dev, uint8_t reg_addr, uint32_t value);

// バス全体をスキャンして、見つかったデバイスを画面に表示する
void ScanAllBus();

void SetupPCI();

// MSI/MSI-X割り込み設定
bool SetupMSI(const Device &dev, uint8_t vector);

} // namespace PCI