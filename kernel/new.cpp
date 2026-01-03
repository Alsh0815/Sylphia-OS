#include "new.hpp"
#include "memory/memory_manager.hpp"

void *operator new(size_t size)
{
    // 実際に確保するサイズ = 要求サイズ + ヘッダサイズ
    size_t total_size = size + kHeaderSize;

    // メモリ確保
    void *ptr = MemoryManager::Allocate(total_size);
    if (!ptr)
    {
        return nullptr;
    }

    // 先頭にサイズを記録する
    // ptr は void* なので、uint64_t* にキャストして書き込む
    *reinterpret_cast<uint64_t *>(ptr) = total_size;

    // 呼び出し元には、ヘッダの分だけ進めたアドレスを返す
    // char* でポインタ演算してから void* に戻すのが安全
    return static_cast<void *>(static_cast<char *>(ptr) + kHeaderSize);
}

// 配列用 new[]
void *operator new[](size_t size)
{
    return operator new(size);
}

void operator delete(void *ptr) noexcept
{
    if (!ptr)
        return;

    // 渡されたポインタは「データ本体」の先頭なので、
    // ヘッダサイズ分だけ「戻って」本来の先頭アドレスを得る
    void *real_ptr =
        static_cast<void *>(static_cast<char *>(ptr) - kHeaderSize);

    // 記録しておいたサイズを読み取る
    size_t total_size = *reinterpret_cast<uint64_t *>(real_ptr);

    // メモリマネージャーに返却
    MemoryManager::Free(real_ptr, total_size);
}

void operator delete(void *ptr, size_t size) noexcept
{
    // C++14以降のサイズ付きdelete
    (void)size; // unused parameter
    operator delete(ptr);
}

void operator delete[](void *ptr) noexcept
{
    operator delete(ptr);
}

void operator delete[](void *ptr, size_t size) noexcept
{
    (void)size; // unused parameter
    operator delete(ptr);
}
