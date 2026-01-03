#pragma once

namespace std
{

// --- RemoveReference ---
// 型から & や && を取り除いて「素の型」を取り出す仕組み
template <typename T> struct RemoveReference
{
    using type = T;
};
template <typename T> struct RemoveReference<T &>
{
    using type = T;
};
template <typename T> struct RemoveReference<T &&>
{
    using type = T;
};

// ヘルパーエイリアス
template <typename T>
using RemoveReferenceT = typename RemoveReference<T>::type;

// --- std::move ---
// どんな値も「右値参照（もう使わない値）」として扱うようにキャストする
template <typename T> constexpr RemoveReferenceT<T> &&move(T &&t) noexcept
{
    return static_cast<RemoveReferenceT<T> &&>(t);
}

// --- std::forward ---
// 完全転送用：引数が本来「左値」だったか「右値」だったかを維持してキャストする
template <typename T> constexpr T &&forward(RemoveReferenceT<T> &t) noexcept
{
    return static_cast<T &&>(t);
}

template <typename T> constexpr T &&forward(RemoveReferenceT<T> &&t) noexcept
{
    // 右値を受け取った場合も、そのまま右値として転送する
    return static_cast<T &&>(t);
}

} // namespace std