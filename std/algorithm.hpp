#pragma once

namespace std
{
template <typename T> constexpr const T &min(const T &a, const T &b)
{
    return (a < b) ? a : b;
}

template <typename T, typename Compare>
constexpr const T &min(const T &a, const T &b, Compare comp)
{
    return comp(a, b) ? a : b;
}
} // namespace std