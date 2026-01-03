#pragma once

#include "new.hpp"
#include <std/utility.hpp>

template <typename T> class Vector
{
public:
    Vector() : _data(nullptr), _size(0), _capacity(0) {}

    ~Vector()
    {
        if (_data)
            delete[] _data;
    }

    // 所有権の移動を伴う追加
    void PushBack(T &&value)
    {
        if (_size >= _capacity)
        {
            Reserve(_capacity == 0 ? 4 : _capacity * 2);
        }
        _data[_size++] =
            std::move(value); // UniquePtrならここで所有権が移動する
    }

    T &operator[](size_t index)
    {
        return _data[index];
    }
    size_t Size() const
    {
        return _size;
    }

private:
    void Reserve(size_t new_capacity)
    {
        // MemoryManager::Allocate を直接使わず new[] を活用する
        T *new_data = new T[new_capacity];
        for (size_t i = 0; i < _size; ++i)
        {
            new_data[i] = std::move(_data[i]);
        }
        delete[] _data;
        _data = new_data;
        _capacity = new_capacity;
    }

    T *_data;
    size_t _size;
    size_t _capacity;
};