#pragma once

template <typename T>
constexpr const T &max(const T &a, const T &b)
{
    return (a < b) ? b : a;
}

template <class T>
inline constexpr const T &min(const T &a, const T &b)
{
    return (b < a) ? b : a;
}