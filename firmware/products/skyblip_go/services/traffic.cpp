#include "products/skyblip_go/services/traffic.h"

namespace skyblip::go {

// The one thing this service knows before any frame arrives: which aircraft is
// this one. A ground relay rebroadcasts everything it heard, us included, and
// the table is where that is refused (core/traffic/table.h).
Status TrafficService::setup() {
    context_.state.traffic.set_own_address(context_.roles.device_addr);
    return Status::Ok;
}

void TrafficService::tick(uint32_t now_ms) {
    messages::RfEvent event{};
    while (context_.bus.rf.pop(event)) {
        switch (event.type) {
            case messages::RfEventType::RxDone: on_frame(event, now_ms); break;
            case messages::RfEventType::CrcError:
            case messages::RfEventType::Missed: context_.state.rx_bad++; break;
            // A busy band is not a fault: it is the state the media access
            // rules exist for, and the transmit policy has to see it.
            case messages::RfEventType::TxBusy: context_.state.tx_busy++; break;
            // The executor's own timestamp, carried alongside the counter it
            // already bumps: RadioService owns the deadline this closes
            // against, and reads it from here rather than a second drain of
            // the same bus.
            case messages::RfEventType::TxDone:
                context_.state.tx_ok++;
                context_.state.last_tx_done_at_us = event.at_us;
                break;
        }
    }
    context_.state.traffic.age_out(context_.state.traffic_now(now_ms));
}

void TrafficService::on_frame(const messages::RfEvent& event, uint32_t now_ms) {
    protocol::Frame frame{};
    const protocol::System system =
        protocol::receive_burst(event.band, event.data.data(), event.len, frame);
    if (system == protocol::System::AdslUplink) {
        on_uplink(event, now_ms);
        return;
    }
    if (system == protocol::System::Unknown) {
        context_.state.rx_bad++;
        return;
    }

    const uint32_t utc = context_.state.traffic_now(now_ms);
    messages::AircraftObs obs{};
    const bool alptas = system == protocol::System::Alptas;
    const bool decoded = alptas ? decode_alptas(frame, utc, obs) : decode_adsl(frame, utc, obs);
    if (!decoded) {
        context_.state.rx_bad++;
        return;
    }

    obs.rx_ms = static_cast<uint16_t>(now_ms % 1000);
    obs.rssi_dbm = event.rssi_dbm;
    context_.state.traffic.update(obs, utc);
    context_.state.rx_ok++;
}

// One frame from the ground, up to thirteen aircraft in it (§C.4's higher rate
// buys the room the M band has no space for), each one an observation in its
// own right.
// The counters are the uplink's own: a codeword Reed-Solomon refuses is not an
// M-band framing failure, and counting it as one is what let this whole path go
// missing without a single number moving.
void TrafficService::on_uplink(const messages::RfEvent& event, uint32_t now_ms) {
    if (!uplink_) return;
    context_.state.uplink_frames++;

    messages::AircraftObs relayed[protocol::AdslUplink::kMaxTargets];
    protocol::AdslUplink::DecodeStats stats{};
    if (uplink_codec_.decode(event.data.data(), relayed, protocol::AdslUplink::kMaxTargets,
                             stats) != Status::Ok) {
        context_.state.uplink_bad++;
        return;
    }

    // The relay's own reception is older than this second by however long the
    // ground station took to compose the frame, and nothing in it says by how
    // much. Stamping it with the second it arrived in is the only honest
    // reading, and core/traffic/table.h is what stops that recency from
    // outranking a direct reception of the same aircraft.
    const uint32_t utc = context_.state.traffic_now(now_ms);
    for (int i = 0; i < stats.targets; i++) {
        messages::AircraftObs& obs = relayed[i];
        obs.rx_utc = utc;
        obs.rx_ms = static_cast<uint16_t>(now_ms % 1000);
        obs.rssi_dbm = event.rssi_dbm;
        if (context_.state.traffic.update(obs, utc) >= 0) context_.state.uplink_targets++;
    }
}

// The Manchester error map travels with the frame, so the forward correction
// knows which bits the air already told us not to trust.
bool TrafficService::decode_adsl(protocol::Frame& frame, uint32_t utc, messages::AircraftObs& obs) {
    protocol::AdslPacket p{};
    p.init();
    __builtin_memcpy(&p.Version, frame.data, protocol::kAdslFrameBytes);
    if (p.check_crc() != 0 && (p.correct(frame.err) < 0 || p.check_crc() != 0)) return false;
    p.descramble();
    protocol::to_obs(p, utc, 0, 0, messages::Source::AdslDirect, obs);
    return true;
}

// ALP-TAS codes position relative to the receiver and keys on the second the
// frame was sent in, so without a fix of our own there is nothing to decode
// against. Slot 1 reaches 200 ms past its own second (§C.5), and a burst caught
// in that tail was keyed to the second before this one, so the previous key is
// the second and last thing to try.
bool TrafficService::decode_alptas(const protocol::Frame& frame, uint32_t utc,
                                   messages::AircraftObs& obs) const {
    const messages::OwnState& own = context_.state.own;
    if (!own.fix_valid || !own.utc_valid) return false;
    if (!protocol::alptas_crc_ok(frame.data)) return false;
    const int32_t lat = own.lat_1e7;
    const int32_t lon = own.lon_1e7;
    if (protocol::alptas_decode(frame.data, utc, lat, lon, obs) == Status::Ok) return true;
    return utc > 0 && protocol::alptas_decode(frame.data, utc - 1, lat, lon, obs) == Status::Ok;
}

}  // namespace skyblip::go
