#pragma once

#include <std/utility.hpp>
#include <stddef.h>

using nullptr_t = decltype(nullptr);

template <typename T> class UniquePtr
{
public:
    // 1. コンストラクタ
    constexpr UniquePtr() noexcept : ptr_(nullptr) {}
    constexpr UniquePtr(nullptr_t) noexcept : ptr_(nullptr) {}
    explicit UniquePtr(T *ptr) noexcept : ptr_(ptr) {}

    // 2. デストラクタ (RAIIの核心: スコープを抜ける時に自動解放)
    ~UniquePtr() noexcept
    {
        Reset();
    }

    // 3. コピー禁止 (所有権が二人以上に分かれるのを防ぐ)
    UniquePtr(const UniquePtr &) = delete;
    UniquePtr &operator=(const UniquePtr &) = delete;

    // 4. ムーブ（所有権の移動）は許可
    UniquePtr(UniquePtr &&other) noexcept : ptr_(other.Release()) {}
    UniquePtr &operator=(UniquePtr &&other) noexcept
    {
        Reset(other.Release());
        return *this;
    }

    // 5. ユーティリティメソッド
    T *Release() noexcept
    {
        T *temp = ptr_;
        ptr_ = nullptr;
        return temp;
    }

    void Reset(T *ptr = nullptr) noexcept
    {
        if (ptr_ != ptr)
        {
            delete ptr_; // kernel/new.cpp で実装した operator delete が呼ばれる
            ptr_ = ptr;
        }
    }

    // 6. ポインタ操作のオーバーロード
    T &operator*() const
    {
        return *ptr_;
    }
    T *operator->() const noexcept
    {
        return ptr_;
    }
    T *Get() const noexcept
    {
        return ptr_;
    }

    // 7. 真偽値判定 (if (ptr) のため)
    explicit operator bool() const noexcept
    {
        return ptr_ != nullptr;
    }

private:
    T *ptr_;
};

// 8. MakeUnique の実装 (安全に生成するためのヘルパー)
template <typename T, typename... Args> UniquePtr<T> MakeUnique(Args &&...args)
{
    return UniquePtr<T>(new T(std::forward<Args>(args)...));
}