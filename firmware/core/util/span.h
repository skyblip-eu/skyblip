// core/util/span.h — a minimal non-owning view (std::span is C++20; we are C++17).
#ifndef SKYBLIP_CORE_UTIL_SPAN_H
#define SKYBLIP_CORE_UTIL_SPAN_H

#include <cstddef>
#include <cstdint>

namespace skyblip {

template <typename T>
class Span {
   public:
    constexpr Span() = default;
    constexpr Span(T* data, size_t size) : data_(data), size_(size) {}
    template <size_t N>
    constexpr Span(T (&arr)[N]) : data_(arr), size_(N) {}

    constexpr T* data() const { return data_; }
    constexpr size_t size() const { return size_; }
    constexpr bool empty() const { return size_ == 0; }
    constexpr T& operator[](size_t i) const { return data_[i]; }
    constexpr T* begin() const { return data_; }
    constexpr T* end() const { return data_ + size_; }

    constexpr Span<T> subspan(size_t offset, size_t count) const {
        return Span<T>(data_ + offset, count);
    }
    constexpr Span<T> first(size_t count) const { return Span<T>(data_, count); }

   private:
    T* data_{nullptr};
    size_t size_{0};
};

using ByteSpan = Span<uint8_t>;
using ConstByteSpan = Span<const uint8_t>;

}

#endif
