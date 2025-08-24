#include "../../io.hpp"
#include "ps2.hpp"

namespace
{
    // コントローラがコマンドを受け付ける準備ができるまで待機
    void wait_for_write()
    {
        while ((inb(0x64) & 0b10) != 0)
        {
        }
    }

    // コントローラからデータが届くまで待機
    void wait_for_read()
    {
        while ((inb(0x64) & 0b1) == 0)
        {
        }
    }
}

void ps2::init()
{
    // === 1. ポートの無効化 ===
    wait_for_write();
    outb(0x64, 0xAD); // キーボードポートを無効化
    wait_for_write();
    outb(0x64, 0xA7); // マウスポートを無効化

    // === 2. 出力バッファをフラッシュ ===
    // ポート無効化時に溜まっている可能性のあるデータを読み飛ばす
    inb(0x60);

    // === 3. コンフィグバイトの設定 ===
    wait_for_write();
    outb(0x64, 0x20); // 「コンフィグバイトを読みます」コマンド
    wait_for_read();
    uint8_t config = inb(0x60); // コンフィグバイトを読み出し

    config |= (1 << 0);  // Bit 0: キーボード割り込み(IRQ1)を有効化
    config |= (1 << 1);  // Bit 1: マウス割り込み(IRQ12)を有効化
    config &= ~(1 << 5); // Bit 5: マウスのクロックを無効化(これは間違いの元なのでクリア)
    config &= ~(1 << 6); // Bit 6: スキャンコード変換を無効化

    wait_for_write();
    outb(0x64, 0x60); // 「コンフィグバイトを書きます」コマンド
    wait_for_write();
    outb(0x60, config); // 変更したコンフィグバイトを書き込み

    // === 4. マウスデバイスの有効化 ===
    wait_for_write();
    outb(0x64, 0xA8); // マウスポートを有効化

    wait_for_write();
    outb(0x64, 0xD4); // 「次のデータはマウスに送ります」コマンド
    wait_for_write();
    outb(0x60, 0xF4); // マウスに「データ送信を有効化せよ」コマンドを送信
    wait_for_read();
    inb(0x60); // マウスから返ってきたACK(0xFA)を読み飛ばす ★重要★

    // === 5. キーボードの有効化 ===
    wait_for_write();
    outb(0x64, 0xAE); // キーボードポートを有効化
}