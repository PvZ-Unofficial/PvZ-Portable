#pragma once
#include "Plugin.h"
#include "PluginLayoutTypes.h"
#include <cstddef>

namespace PvzpPlugin
{
constexpr std::uint64_t LayoutName(const char* text)
{
    std::uint64_t hash = 14695981039346656037ULL;
    while (*text)
    {
        hash ^= static_cast<unsigned char>(*text++);
        hash *= 1099511628211ULL;
    }
    return hash;
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
struct LayoutInspector
{
    template<class T> static constexpr std::size_t Items() { return offsetof(DataArray<T>, mItems); }
    template<class T> static constexpr std::size_t Ids() { return offsetof(DataArray<T>, mItemIds); }
    template<class T> static constexpr std::size_t Stride() { return sizeof(typename DataArray<T>::DataArrayItem); }
    template<class T> static constexpr std::size_t ItemAlign() { return alignof(typename DataArray<T>::DataArrayItem); }
};

// All bases in the SDK are nonvirtual. This computes only the compiler's
// constant pointer adjustment; it never dereferences or constructs a game object.
template<class Derived, class Base> inline std::uint64_t BaseOffset()
{
    return reinterpret_cast<std::uintptr_t>(static_cast<Base*>(reinterpret_cast<Derived*>(0x10000))) - 0x10000;
}

#define PVZP_LAYOUT_TYPE(T) {LayoutName(#T ".size"), sizeof(T)}, {LayoutName(#T ".align"), alignof(T)},
#define PVZP_LAYOUT_FIELD(T, F) {LayoutName(#T "." #F), offsetof(T, F)}, {LayoutName(#T "." #F ".size"), sizeof(decltype(T::F))},
#define PVZP_LAYOUT_BASE(T, B) {LayoutName(#T ".base." #B), BaseOffset<T, B>()}, PVZP_LAYOUT_TYPE(B)
#define PVZP_LAYOUT_POOL(T) \
    PVZP_LAYOUT_TYPE(DataArray<T>) \
    {LayoutName("DataArray<" #T ">.mItems"), LayoutInspector::Items<T>()}, \
    {LayoutName("DataArray<" #T ">.mItemIds"), LayoutInspector::Ids<T>()}, \
    {LayoutName("DataArray<" #T ">.stride"), LayoutInspector::Stride<T>()}, \
    {LayoutName("DataArray<" #T ">.item.align"), LayoutInspector::ItemAlign<T>()}, \
    PVZP_LAYOUT_FIELD(DataArray<T>, mMaxUsedCount) \
    PVZP_LAYOUT_FIELD(DataArray<T>, mMaxSize) \
    PVZP_LAYOUT_FIELD(DataArray<T>, mFreeListHead) \
    PVZP_LAYOUT_FIELD(DataArray<T>, mSize) \
    PVZP_LAYOUT_FIELD(DataArray<T>, mNextKey) \
    PVZP_LAYOUT_FIELD(DataArray<T>, mName)

inline const LayoutEntry Layout[] = {
    {LayoutName("pointer.size"), sizeof(void*)},
#ifdef _MSC_VER
    {LayoutName("abi.msvc"), 1},
#else
    {LayoutName("abi.gnu"), 1},
#endif
    PVZP_LAYOUT_TYPE(std::string)
#include "PluginLayoutFields.inc"
    PVZP_LAYOUT_TYPE(Rect)
    PVZP_LAYOUT_FIELD(Rect, mX)
    PVZP_LAYOUT_FIELD(Rect, mY)
    PVZP_LAYOUT_FIELD(Rect, mWidth)
    PVZP_LAYOUT_FIELD(Rect, mHeight)
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__aarch64__))
    PVZP_LAYOUT_FIELD(GameObject, mAbiLayoutPadding)
#endif
};

#undef PVZP_LAYOUT_POOL
#undef PVZP_LAYOUT_BASE
#undef PVZP_LAYOUT_FIELD
#undef PVZP_LAYOUT_TYPE
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
}
