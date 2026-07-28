#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_LINK_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_LINK_H

#include <string>
#include <vector>

#include "core/bus/bus.h"
#include "hal/link.h"

namespace skyblip::platform::host {

class Link : public hal::Link {
   public:
    struct Frame {
        messages::Endpoint endpoint;
        std::string bytes;
    };

    Status begin() { return Status::Ok; }

    void push_rx(const messages::RxFrame& frame) { rx_.push(frame); }
    bool pop_rx(messages::RxFrame& out) { return rx_.pop(out); }

    Status send(messages::Endpoint ep, ConstByteSpan bytes) override {
        if (next_status_ != Status::Ok) {
            Status s = next_status_;
            if (once_) next_status_ = Status::Ok;
            return s;
        }
        sent.push_back(
            {ep, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size())});
        return Status::Ok;
    }

    void force_status(Status s, bool once = true) {
        next_status_ = s;
        once_ = once;
    }

    const Frame& last() const { return sent.back(); }
    bool last_on(messages::Endpoint ep) const {
        return !sent.empty() && sent.back().endpoint == ep;
    }
    int count_on(messages::Endpoint ep) const {
        int n = 0;
        for (auto& f : sent)
            if (f.endpoint == ep) n++;
        return n;
    }
    void clear() { sent.clear(); }

    std::vector<Frame> sent;

   private:
    bus::Queue<messages::RxFrame, 4> rx_;
    Status next_status_{Status::Ok};
    bool once_{true};
};

}

#endif
