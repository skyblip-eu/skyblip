// core/comms/link_session.h: what a companion connection looks like from the
// firmware's side - the one place a connect, a disconnect and a negotiated
// payload figure become messages::LinkEvent.
//
// It lives here, framework-free and host-tested, because the alternative is the
// rule living twice: once inside a Bluetooth callback on silicon and once in a
// test helper on the host. That is what left the tree with a bus message no
// board ever raised. Both platforms own the critical section around it (§5.3) and
// neither owns the policy.
#ifndef SKYBLIP_CORE_COMMS_LINK_SESSION_H
#define SKYBLIP_CORE_COMMS_LINK_SESSION_H

#include "core/messages/messages.h"
#include "core/util/fifo.h"
#include "hal/link.h"

namespace skyblip::comms {

class LinkSession {
   public:
    // A connect, a late MTU exchange and a disconnect is three, and a reader that
    // missed a pass must still find the pair that framed it.
    static constexpr size_t kEventCapacity = 5;

    void connected(uint16_t session_id, uint16_t payload_bytes) {
        // A central connecting while one is already up would otherwise leave two
        // Ups and no Down between them, and a reader counting sessions would
        // never come back down. The peripheral role allows one connection, so
        // this is the invariant being kept, not a case being handled.
        if (up_) raise(messages::LinkEventType::Down, session_, 0);
        session_ = session_id;
        payload_bytes_ = floor_payload(payload_bytes);
        up_ = true;
        raise(messages::LinkEventType::Up, session_, payload_bytes_);
    }

    // INFO: le 04aug26 The MTU exchange lands after the connection is up, so the
    // figure the link came up with is not the figure it will carry: this raises a
    // second Up on the SAME session id, which is how a reader tells a refreshed
    // payload from a new central. hal::Link::payload_bytes() stays the authority
    // for a frame being formatted now; this is the same number, on the bus, at
    // the moment it changed.
    void payload_changed(uint16_t payload_bytes) {
        const uint16_t figure = floor_payload(payload_bytes);
        if (!up_ || figure == payload_bytes_) return;
        payload_bytes_ = figure;
        raise(messages::LinkEventType::Up, session_, payload_bytes_);
    }

    void disconnected(uint16_t session_id) {
        if (!up_ || session_id != session_) return;
        up_ = false;
        raise(messages::LinkEventType::Down, session_, 0);
    }

    bool pop(messages::LinkEvent& out) {
        Result<messages::LinkEvent> event = events_.pop();
        if (!event.ok()) return false;
        out = event.value();
        return true;
    }

    bool up() const { return up_; }
    uint16_t session_id() const { return session_; }
    uint16_t payload_bytes() const { return payload_bytes_; }
    // A lifecycle event that did not fit is a service left believing the wrong
    // thing, so it is counted here the way core/bus counts an overflow.
    uint32_t dropped() const { return dropped_; }

   private:
    static uint16_t floor_payload(uint16_t bytes) {
        return bytes < hal::kMinimumLinkPayload ? hal::kMinimumLinkPayload : bytes;
    }

    void raise(messages::LinkEventType type, uint16_t session_id, uint16_t payload_bytes) {
        messages::LinkEvent event{};
        event.type = type;
        event.session_id = session_id;
        event.payload_bytes = payload_bytes;
        if (!is_ok(events_.push(event))) dropped_++;
    }

    Fifo<messages::LinkEvent, kEventCapacity> events_{};
    uint16_t session_{0};
    uint16_t payload_bytes_{hal::kMinimumLinkPayload};
    uint32_t dropped_{0};
    bool up_{false};
};

}

#endif
