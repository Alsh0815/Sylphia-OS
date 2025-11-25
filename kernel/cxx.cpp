#include <stdint.h>

extern "C"
{
    // 1. 純粋仮想関数エラー (virtual function call error)
    // 実装がない仮想関数を呼び出した時にコンパイラがここを呼ぶ
    void __cxa_pure_virtual()
    {
        while (1)
            __asm__ volatile("hlt");
    }

    // 2. static変数の初期化ガード (今回のエラーの原因)
    // static変数が「1回だけ」初期化されることを保証する仕組み。
    // 本来はマルチスレッド排他制御が必要だが、シングルスレッドOSならフラグ管理だけでOK。

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

    // 3. グローバル/staticオブジェクトのデストラクタ登録
    // OS終了時に呼ばれるものだが、OSは終了しないので何もしないでOK
    void *__dso_handle = 0;

    int __cxa_atexit(void (*destructor)(void *), void *arg, void *dso)
    {
        return 0; // 成功したふりをする
    }
}