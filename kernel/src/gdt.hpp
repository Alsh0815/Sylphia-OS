#pragma once
#include <stdint.h>

// 64bit TSS
struct __attribute__((packed)) Tss64
{
    uint32_t rsv0;
    uint64_t rsp0, rsp1, rsp2;
    uint64_t rsv1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t rsv2;
    uint16_t rsv3;
    uint16_t io_map_base; // 末尾
};

namespace gdt
{

    // 引数: IST1 のスタックトップ（高アドレス側）
    // 戻り: true=成功
    bool init(uint64_t ist1_top);

    // 参考: 現在のCSセレクタ（IDT側で使いたければ）
    uint16_t cs();

}
