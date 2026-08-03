#ifndef SKYBLIP_CORE_BUS_BUS_H
#define SKYBLIP_CORE_BUS_BUS_H

#include <cstdint>

#include "core/gnss/nmea.h"
#include "core/messages/messages.h"

namespace skyblip::bus {

template <class T, int N>
class Queue {
   public:
    static constexpr int kCapacity = N;

    bool push(const T& item) {
        const int next = (head_ + 1) % (N + 1);
        if (next == tail_) {
            dropped_++;
            return false;
        }
        slot_[head_] = item;
        head_ = next;
        return true;
    }

    bool pop(T& out) {
        if (tail_ == head_) return false;
        out = slot_[tail_];
        tail_ = (tail_ + 1) % (N + 1);
        return true;
    }

    bool empty() const { return tail_ == head_; }
    uint32_t dropped() const { return dropped_; }

   private:
    T slot_[N + 1]{};
    int head_{0};
    int tail_{0};
    uint32_t dropped_{0};
};

struct Bus {
    Queue<gnss::GnssFix, 2> gnss;
    Queue<messages::BaroSample, 2> baro;
    Queue<messages::RfEvent, 8> rf;
    Queue<messages::RxFrame, 4> link_rx;
    // One writer, one reader, per §5.3: the config service drains link_rx with a
    // while-pop, so a log command sharing that queue would be read and dropped
    // by the wrong service.
    Queue<messages::RxFrame, 2> log_rx;
    Queue<messages::ButtonEvent, 4> input;
    Queue<messages::BatterySample, 2> battery;
};

}  // namespace skyblip::bus

#endif
