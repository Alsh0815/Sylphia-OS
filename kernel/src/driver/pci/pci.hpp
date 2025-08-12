#pragma once
#include <stdint.h>

struct Console; // 前方宣言（ログ用）

namespace pci
{

    struct Device
    {
        uint8_t bus, dev, func;
        uint16_t vendor, device;
        uint8_t class_code, subclass, prog_if, rev_id;
        uint8_t header_type;
        uint64_t bar[6]; // 64bitに正規化（未設定は0）
    };

    // 単発レジスタアクセス（DWORDアラインのみを想定）
    uint32_t read_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
    void write_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val);

    // 全バス列挙してNVMeらしきデバイスを探し、見つけたらログ出力
    // 見つかったら true を返し、最後に見つけた1台の情報を out に入れる
    bool scan_nvme(Console &con, Device &out);

    // 任意のデバイスに「メモリ空間有効＋バスマスタ」を有効化
    void enable_mem_busmaster(const Device &d);

}
