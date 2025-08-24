#pragma once

#include "kernel_runtime.hpp"
#include "pmm.hpp"

// ---- PMM ベースの最小限 vector 風コンテナ（例外なし・再配置あり） ----
template <typename T>
class PmmVec
{
public:
    PmmVec() : _ptr(nullptr), _size(0), _cap_bytes(0), _pages(0) {}
    ~PmmVec() { release(); }

    // コピー禁止（必要なら実装）
    PmmVec(const PmmVec &) = delete;
    PmmVec &operator=(const PmmVec &) = delete;

    // ムーブ可（任意）
    PmmVec(PmmVec &&o) noexcept { move_from(o); }
    PmmVec &operator=(PmmVec &&o) noexcept
    {
        if (this != &o)
        {
            release();
            move_from(o);
        }
        return *this;
    }

    bool push_back(const T &v)
    {
        if (!ensure_room(1))
            return false;
        _ptr[_size++] = v;
        return true;
    }

    void clear() { _size = 0; }
    size_t size() const { return _size; }
    bool empty() const { return _size == 0; }

    T &operator[](size_t i) { return _ptr[i]; }
    const T &operator[](size_t i) const { return _ptr[i]; }
    T &back() { return _ptr[_size - 1]; }
    const T &back() const { return _ptr[_size - 1]; }

    T *data() { return _ptr; }
    const T *data() const { return _ptr; }

    T *begin() { return _ptr; }
    T *end() { return _ptr + _size; }
    const T *begin() const { return _ptr; }
    const T *end() const { return _ptr + _size; }

    void release()
    {
        if (_ptr && _pages)
        {
            pmm::free_pages(_ptr, _pages);
        }
        _ptr = nullptr;
        _cap_bytes = 0;
        _pages = 0;
        _size = 0;
    }

private:
    // N 個追加できる空きを確保。ページ倍増で拡張。
    bool ensure_room(size_t n_more)
    {
        const size_t need = (_size + n_more) * sizeof(T);
        if (need <= _cap_bytes)
            return true;

        // 初回は 1 ページ、以降は倍々 or 必要量まで
        size_t new_cap = (_cap_bytes == 0) ? 4096 : _cap_bytes;
        while (new_cap < need)
            new_cap <<= 1;

        // ページ数に切り上げ
        const uint64_t page = 4096;
        uint64_t new_pages = (new_cap + page - 1) / page;
        void *new_mem = pmm::alloc_pages(new_pages);
        if (!new_mem)
            return false;

        // 旧データをコピー
        if (_ptr && _size)
        {
            // T が trivially copyable 前提（今回 Extent 等の POD 用途）
            memcpy(new_mem, _ptr, _size * sizeof(T));
        }

        // 旧領域を解放
        if (_ptr && _pages)
        {
            pmm::free_pages(_ptr, _pages);
        }

        _ptr = reinterpret_cast<T *>(new_mem);
        _pages = new_pages;
        _cap_bytes = new_pages * page;
        return true;
    }

    void move_from(PmmVec &o)
    {
        _ptr = o._ptr;
        _size = o._size;
        _cap_bytes = o._cap_bytes;
        _pages = o._pages;
        o._ptr = nullptr;
        o._size = 0;
        o._cap_bytes = 0;
        o._pages = 0;
    }

    T *_ptr;
    size_t _size;
    size_t _cap_bytes;
    uint64_t _pages;
};
