#include "products/skyblip_go/services/traffic.h"

namespace skyblip::go {

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
            case messages::RfEventType::TxDone: context_.state.tx_ok++; break;
        }
    }
    context_.state.traffic.age_out(context_.state.traffic_now(now_ms));
}

void TrafficService::on_frame(const messages::RfEvent& event, uint32_t now_ms) {
    protocol::Frame frame{};
    if (!protocol::receive_mband(event.data.data(), event.len, frame)) {
        context_.state.rx_bad++;
        return;
    }

    const uint32_t utc = context_.state.traffic_now(now_ms);
    messages::AircraftObs obs{};
    const bool decoded = frame.system == protocol::System::Alptas
                             ? decode_alptas(frame, utc, obs)
                             : decode_adsl(frame, utc, obs);
    if (!decoded) {
        context_.state.rx_bad++;
        return;
    }

    obs.rx_ms = static_cast<uint16_t>(now_ms % 1000);
    obs.rssi_dbm = event.rssi_dbm;
    context_.state.traffic.update(obs, utc);
    context_.state.rx_ok++;
}

// The Manchester error map travels with the frame, so the forward correction
// knows which bits the air already told us not to trust.
bool TrafficService::decode_adsl(protocol::Frame& frame, uint32_t utc,
                                 messages::AircraftObs& obs) {
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
    if (protocol::alptas_decode(frame.data, utc, own.lat_1e7, own.lon_1e7, obs) == Status::Ok)
        return true;
    return utc > 0 && protocol::alptas_decode(frame.data, utc - 1, own.lat_1e7, own.lon_1e7,
                                              obs) == Status::Ok;
}

}  // namespace skyblip::go
