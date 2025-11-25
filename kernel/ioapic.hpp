#pragma once
#include <stdint.h>

// I/O APICのレジスタ操作
class IOAPIC
{
public:
    // I/O APICの初期化 (今回はアドレス固定で実装)
    static void Init();

    // IRQ番号(irq)を、割り込みベクタ(vector)に割り当て、指定したAPIC ID(dest_id)へ送る設定をする
    static void Enable(int irq, int vector, uint32_t dest_id);

private:
    static uint32_t Read(uint32_t index);
    static void Write(uint32_t index, uint32_t data);
};