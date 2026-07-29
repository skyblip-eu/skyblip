#include "products/skyblip_go/services/traffic.h"

#include "core/protocol/adsl.h"

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
    if (event.len < protocol::AdslPacket::kTxBytes) {
        context_.state.rx_bad++;
        return;
    }

    protocol::AdslPacket p{};
    __builtin_memcpy(&p, event.data.data(), protocol::AdslPacket::kTxBytes);

    if (p.check_crc() != 0) {
        uint8_t err[protocol::AdslPacket::kDataBytes] = {0};
        if (p.correct(err) < 0 || p.check_crc() != 0) {
            context_.state.rx_bad++;
            return;
        }
    }
    p.descramble();

    const uint32_t utc = context_.state.traffic_now(now_ms);
    messages::AircraftObs obs{};
    protocol::to_obs(p, utc, static_cast<uint16_t>(now_ms % 1000), event.rssi_dbm,
                     messages::Source::AdslDirect, obs);
    context_.state.traffic.update(obs, utc);
    context_.state.rx_ok++;
}

}  // namespace skyblip::go
