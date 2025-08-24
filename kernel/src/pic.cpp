#include "io.hpp"
#include "pic.hpp"

void initialize_pic()
{
    // マスターPIC、スレーブPICを待機させる
    outb(PIC0_IMR, 0xff);
    outb(PIC1_IMR, 0xff);

    // ICW1: 初期化開始を通知 (コマンドポートへ)
    outb(PIC0_ICW1, 0x11); // カスケード接続、ICW4が必要
    outb(PIC1_ICW1, 0x11);

    // ICW2: 割り込みベクタ番号のマッピング (データポートへ)
    outb(PIC0_ICW2, 0x20); // IRQ0-7  -> 0x20-0x27
    outb(PIC1_ICW2, 0x28); // IRQ8-15 -> 0x28-0x2F

    // ICW3: マスターとスレーブの接続情報 (データポートへ)
    outb(PIC0_ICW3, 0x04); // スレーブPICはIRQ2に接続されている
    outb(PIC1_ICW3, 0x02); // 自分はマスターのIRQ2に接続されている

    // ICW4: 動作モード設定 (データポートへ)
    outb(PIC0_ICW4, 0x01); // 8086/88モード
    outb(PIC1_ICW4, 0x01);

    // IMR (Interrupt Mask Register): 割り込みのマスク設定
    // IRQ1 (キーボード) と IRQ12 (マウス) 以外はマスクする
    outb(PIC0_IMR, 0b11111001); // IRQ1(キーボード)とIRQ2(スレーブ接続用)を有効化
    outb(PIC1_IMR, 0b11101111); // IRQ12(マウス)を有効化
}