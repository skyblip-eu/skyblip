// core/util/fifo.h: bounded single-producer/single-consumer ring buffer.
#ifndef SKYBLIP_CORE_UTIL_FIFO_H
#define SKYBLIP_CORE_UTIL_FIFO_H

#include <array>
#include <cstddef>

#include "core/util/result.h"

namespace skyblip {

template <typename T, size_t Capacity>
class Fifo {
   public:
    static_assert(Capacity >= 2, "fifo needs room for at least one element");

    bool empty() const { return head_ == tail_; }
    bool full() const { return next(head_) == tail_; }
    size_t size() const { return (head_ + Capacity - tail_) % Capacity; }
    static constexpr size_t capacity() { return Capacity - 1; }

    Status push(const T& item) {
        size_t n = next(head_);
        if (n == tail_) return Status::Full;
        buf_[head_] = item;
        head_ = n;
        return Status::Ok;
    }

    Result<T> pop() {
        if (empty()) return Status::Empty;
        T item = buf_[tail_];
        tail_ = next(tail_);
        return item;
    }

    void clear() { head_ = tail_ = 0; }

   private:
    static constexpr size_t next(size_t i) { return (i + 1) % Capacity; }
    std::array<T, Capacity> buf_{};
    size_t head_{0};
    size_t tail_{0};
};

}

#endif
