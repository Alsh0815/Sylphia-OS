#include <stddef.h>
#include <stdint.h>

#include "arch/inasm.hpp"
#include "cxx.hpp"

extern "C"
{
    // 純粋仮想関数エラー (virtual function call error)
    void __cxa_pure_virtual()
    {
        while (1)
            Hlt();
    }

    // static変数の初期化ガード (今回のエラーの原因)
    int __cxa_guard_acquire(long long *guard)
    {
        // guardの指す値が0なら「まだ初期化されていない」ので1を返す(初期化権限を取得)
        if (*guard)
            return 0;
        return 1;
    }

    void __cxa_guard_release(long long *guard)
    {
        // 初期化完了をマークする
        *guard = 1;
    }

    void __cxa_guard_abort(long long *guard)
    {
        // 初期化中に例外が出た場合など (今回は何もしない)
    }

    // グローバル/staticオブジェクトのデストラクタ登録
    void *__dso_handle = 0;

    int __cxa_atexit(void (*destructor)(void *), void *arg, void *dso)
    {
        return 0; // 成功したふりをする
    }

    void *memcpy(void *dest, const void *src, size_t n)
    {
        uint64_t *d8 = (uint64_t *)dest;
        const uint64_t *s8 = (const uint64_t *)src;

        size_t n8 = n / 8;
        while (n8--)
            *d8++ = *s8++;

        char *d1 = (char *)d8;
        const char *s1 = (const char *)s8;
        size_t n1 = n % 8;
        while (n1--)
            *d1++ = *s1++;
        return dest;
    }

    void *memset(void *s, int c, size_t n)
    {
        unsigned char *p = (unsigned char *)s;
        while (n--)
            *p++ = (unsigned char)c;
        return s;
    }
}