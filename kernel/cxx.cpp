#include <stddef.h>
#include <stdint.h>

extern "C"
{
    // 純粋仮想関数エラー (virtual function call error)
    void __cxa_pure_virtual()
    {
        while (1)
            __asm__ volatile("hlt");
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
        char *d = (char *)dest;
        const char *s = (const char *)src;
        while (n--)
            *d++ = *s++;
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