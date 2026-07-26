// devices/host/fake_link.h — an observable + drivable hal::Link test spy (§3).
#ifndef SKYBLIP_DEVICES_HOST_FAKE_LINK_H
#define SKYBLIP_DEVICES_HOST_FAKE_LINK_H

#include <string>
#include <vector>

#include "hal/link.h"

namespace skyblip::host {

class FakeLink : public hal::Link {
   public:
    struct Frame {
        messages::Endpoint endpoint;
        std::string bytes;
    };

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
    Status next_status_{Status::Ok};
    bool once_{true};
};

}

#endif
