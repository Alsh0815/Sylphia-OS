#pragma once
#include <stddef.h>
#include <stdint.h>
#include "memory/memory.hpp"

class Bitmap
{
public:
    void SetBuffer(uint8_t *buffer, size_t size)
    {
        buffer_ = buffer;
        bitmap_size_ = size; // バイト単位のサイズ
    }

    // 指定したビット(フレーム番号)が使用中かどうか
    bool Get(size_t index) const
    {
        if (index / 8 >= bitmap_size_)
            return true; // 範囲外は使用中扱い
        return (buffer_[index / 8] & (1 << (index % 8))) != 0;
    }

    // 指定したビットを使用中(true)または空き(false)にする
    void Set(size_t index, bool allocated)
    {
        if (index / 8 >= bitmap_size_)
            return;
        if (allocated)
        {
            buffer_[index / 8] |= (1 << (index % 8));
        }
        else
        {
            buffer_[index / 8] &= ~(1 << (index % 8));
        }
    }

    // 最初に見つかった空きビットのインデックスを返す (なければ -1)
    long FindFreeFrame() const
    {
        for (size_t i = 0; i < bitmap_size_; ++i)
        {
            if (buffer_[i] != 0xFF) // 全部埋まっていなければ探す
            {
                for (int bit = 0; bit < 8; ++bit)
                {
                    if (!((buffer_[i] >> bit) & 1))
                    {
                        return i * 8 + bit;
                    }
                }
            }
        }
        return -1;
    }

private:
    uint8_t *buffer_ = nullptr;
    size_t bitmap_size_ = 0;
};

class MemoryManager
{
public:
    // フレーム(4KB)単位の定数
    static const size_t kFrameSize = 4096;

    // メモリマップを受け取り、ビットマップを初期化する
    static void Initialize(const MemoryMap &memmap);

    // 指定バイト数を確保 (簡易的にページ単位で切り上げ)
    // alignmentは互換性のための引数で、現状は無視される
    static void *Allocate(size_t size, size_t alignment = 16);

    // メモリを解放する (ビットマップを0に戻す)
    static void Free(void *ptr, size_t size); // サイズが必要になります

    // ページ単位での確保・解放 (内部用兼、将来のページング用)
    static void *AllocateFrame();
    static void FreeFrame(void *ptr);

private:
    static Bitmap bitmap_;
    static uintptr_t range_begin_; // 管理するメモリ領域の開始アドレス(物理)
    static uintptr_t range_end_;   // 管理するメモリ領域の終了アドレス(物理)
};