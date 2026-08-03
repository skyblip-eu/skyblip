#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_LINK_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_LINK_H

#include <string>
#include <vector>

#include "core/bus/bus.h"
#include "core/comms/link_session.h"
#include "hal/link.h"

namespace skyblip::platform::host {

class Link : public hal::Link {
   public:
    struct Frame {
        messages::Endpoint endpoint;
        std::string bytes;
    };

    // INFO: fc 04aug26 A phone that negotiated ATT_MTU 247, which is what an
    // Android central and a nRF Connect both ask for. Cases that care about the
    // ends of the range say so with declare_payload_bytes().
    static constexpr uint16_t kDefaultPayloadBytes = 244;

    Status begin() { return Status::Ok; }

    void push_rx(const messages::RxFrame& frame) { rx_.push(frame); }
    bool pop_rx(messages::RxFrame& out) { return rx_.pop(out); }

    // A central connects, and a central goes away. The same comms::LinkSession
    // the Bluetooth callbacks drive on silicon, so a host case raises a link
    // through the code the device runs rather than through a service call no
    // board makes.
    void raise_link(uint16_t session_id = 1) { session_.connected(session_id, payload_bytes_); }
    void drop_link() { session_.disconnected(session_.session_id()); }
    bool pop_event(messages::LinkEvent& out) { return session_.pop(out); }
    bool up() const { return session_.up(); }
    uint16_t session_id() const { return session_.session_id(); }

    // What this link came up with. Floored the way a real one is: nothing may
    // model a central that offers less than BLE guarantees. Said while a link is
    // up it is the MTU exchange landing late, which is what an iOS central does,
    // and it reaches the bus the same way silicon's att_mtu_updated does.
    void declare_payload_bytes(uint16_t bytes) {
        payload_bytes_ = bytes < hal::kMinimumLinkPayload ? hal::kMinimumLinkPayload : bytes;
        session_.payload_changed(payload_bytes_);
    }

    uint16_t payload_bytes() const override { return payload_bytes_; }

    Status send(messages::Endpoint ep, ConstByteSpan bytes) override {
        // The controller's refusal, modelled: an oversized notification is not
        // shortened, it fails, so no case can pass by sending one.
        if (bytes.size() > payload_bytes_) {
            refused_oversize++;
            return Status::OutOfRange;
        }
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
    int refused_oversize{0};

   private:
    bus::Queue<messages::RxFrame, 4> rx_;
    comms::LinkSession session_{};
    uint16_t payload_bytes_{kDefaultPayloadBytes};
    Status next_status_{Status::Ok};
    bool once_{true};
};

}

#endif
