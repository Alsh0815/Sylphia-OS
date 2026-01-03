#pragma once

#include "new.hpp"
#include <stddef.h>
#include <stdint.h>

enum class ResultStatus
{
    Ok,
    Err
};

template <typename T> struct Ok
{
    T value;
};
template <typename E> struct Err
{
    E value;
};

template <typename T, typename E> class Result
{
private:
    ResultStatus status;
    alignas(T) alignas(
        E) unsigned char storage[sizeof(T) > sizeof(E) ? sizeof(T) : sizeof(E)];

public:
    Result(Ok<T> &&ok) : status(ResultStatus::Ok)
    {
        new (storage) T(ok.value);
    }

    Result(Err<E> &&err) : status(ResultStatus::Err)
    {
        new (storage) E(err.value);
    }

    ~Result()
    {
        if (is_ok())
            reinterpret_cast<T *>(storage)->~T();
        else
            reinterpret_cast<E *>(storage)->~E();
    }

    T &value()
    {
        return *reinterpret_cast<T *>(storage);
    }
    E &error()
    {
        return *reinterpret_cast<E *>(storage);
    }

    bool is_ok() const
    {
        return status == ResultStatus::Ok;
    }
    bool is_err() const
    {
        return status == ResultStatus::Err;
    }
};