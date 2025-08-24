#include <cstdint>

extern "C"
{
    void __cxa_pure_virtual()
    {
        while (1)
        {
            asm volatile("cli; hlt");
        }
    }

    /**
     * @brief 静的オブジェクトのデストラクタを登録する関数。
     * カーネルは traditional な意味で終了しないため、何もしないスタブ関数で問題ありません。
     */
    int __cxa_atexit(void (*destructor)(void *), void *arg, void *dso_handle)
    {
        (void)destructor;
        (void)arg;
        (void)dso_handle;
        return 0;
    }

    /**
     * @brief 静的変数の初期化が完了したかをチェックし、ロックを試みる。
     * @param guard_object コンパイラが用意する64ビットのガード変数へのポインタ。
     * @return 初期化が必要なら1、不要なら0を返す。
     *
     * シングルスレッド環境なので、単純なフラグチェックで十分です。
     * ガード変数の最初の1バイトをフラグとして使います (0:未初期化, 1:初期化完了)。
     */
    int __cxa_guard_acquire(uint64_t *guard_object)
    {
        if (*(reinterpret_cast<volatile uint8_t *>(guard_object)) == 0)
        {
            // まだ初期化されていないので、呼び出し元に初期化を促すために1を返す
            return 1;
        }
        // 初期化済みなので0を返す
        return 0;
    }

    /**
     * @brief 静的変数の初期化が完了したことをマークし、ロックを解除する。
     * @param guard_object コンパイラが用意する64ビットのガード変数へのポインタ。
     */
    void __cxa_guard_release(uint64_t *guard_object)
    {
        // 初期化が完了したので、フラグを1に設定する
        *(reinterpret_cast<volatile uint8_t *>(guard_object)) = 1;
    }
}