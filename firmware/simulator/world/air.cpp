#include "simulator/world/air.h"

#include <cstdio>
#include <cstring>

namespace skyblip::simulator {

namespace {
const char* kEventName[] = {"TX  ", "RX  ", "DEAF", "COLL"};

// The radio reports the frequency its PLL word resolves to, a few hertz off the
// channel it was asked for.
const char* channel_name(uint32_t freq_hz) {
    if (Air::tuned_to(freq_hz, timing::kObandHz)) return "869.525";
    if (Air::tuned_to(freq_hz, timing::kMband1Hz)) return "868.400";
    return "868.200";
}
}  // namespace

void Air::emit(uint64_t at_us, uint32_t freq_hz, const uint8_t* chips, uint16_t len,
               int8_t rssi_dbm, uint32_t bitrate) {
    for (Burst& b : burst_) {
        if (b.used) continue;
        b = Burst{};
        b.used = true;
        b.at_us = at_us;
        b.freq_hz = freq_hz;
        b.rssi_dbm = rssi_dbm;
        b.bitrate = bitrate;
        b.len = len > sizeof(b.chips) ? static_cast<uint16_t>(sizeof(b.chips)) : len;
        b.air_time_us = air_time_us(b.len, bitrate);
        std::memcpy(b.chips, chips, b.len);
        return;
    }
}

void Air::step(uint64_t now_us, models::Sx1262& radio) {
    take_own_transmission(now_us, radio);
    if (tx_in_flight_ && now_us >= tx_done_at_us_) {
        tx_in_flight_ = false;
        radio.signal_tx_done();
    }

    for (Burst& b : burst_) {
        if (!b.used || b.started || now_us < b.at_us) continue;
        b.started = true;
        // A receiver has to be tuned to the channel when the preamble arrives,
        // not when the frame ends.
        b.heard = !b.mine && radio.receiving && tuned_to(radio.freq_hz, b.freq_hz) &&
                  b.rssi_dbm > kSensitivityDbm;
        for (Burst& other : burst_) {
            if (&other == &b || !other.used || !other.started) continue;
            if (!tuned_to(other.freq_hz, b.freq_hz)) continue;
            if (b.at_us < other.at_us + other.air_time_us) {
                b.collided = true;
                other.collided = true;
            }
        }
    }

    for (Burst& b : burst_) {
        if (!b.used || !b.started || now_us < b.at_us + b.air_time_us) continue;
        if (b.mine) {
            log(b, AirEvent::Tx);
        } else if (!b.heard) {
            deaf_++;
            log(b, AirEvent::Deaf);
        } else if (b.collided) {
            collisions_++;
            log(b, AirEvent::Collision);
            radio.receive_air(b.chips, b.len, /*crc_error=*/true, b.rssi_dbm, b.bitrate);
        } else if (radio.receive_air(b.chips, b.len, /*crc_error=*/false, b.rssi_dbm, b.bitrate)) {
            heard_++;
            log(b, AirEvent::Rx);
        } else {
            // Tuned, listening, and still nothing: the detector was armed for a
            // sync word this burst does not carry.
            deaf_++;
            log(b, AirEvent::Deaf);
        }
        b.used = false;
    }

    set_carrier(now_us, radio);
}

// Own-ship's burst is air like any other: it occupies the channel, it collides,
// and the radio only reports TxDone once it has actually been sent.
void Air::take_own_transmission(uint64_t now_us, models::Sx1262& radio) {
    uint8_t chips[protocol::kTxChipBytes] = {0};
    uint8_t len = 0;
    if (!radio.take_tx(chips, len)) return;
    if (len > sizeof(chips)) len = static_cast<uint8_t>(sizeof(chips));
    emit(now_us, radio.freq_hz, chips, len, 0);
    for (Burst& b : burst_)
        if (b.used && !b.started && b.at_us == now_us && b.freq_hz == radio.freq_hz) b.mine = true;
    tx_in_flight_ = true;
    tx_done_at_us_ = now_us + kAirTimeUs;
}

void Air::set_carrier(uint64_t now_us, models::Sx1262& radio) {
    int8_t level = kNoiseFloorDbm;
    for (const Burst& b : burst_) {
        if (!b.used || b.mine) continue;
        if (now_us < b.at_us || now_us >= b.at_us + b.air_time_us) continue;
        if (!tuned_to(radio.freq_hz, b.freq_hz)) continue;
        if (b.rssi_dbm > level) level = b.rssi_dbm;
    }
    radio.rssi_dbm = level;
}

void Air::log(const Burst& b, AirEvent event) {
    AirRecord& r = log_[count_ % kLogSize];
    r.at_us = b.at_us;
    r.freq_hz = b.freq_hz;
    // The phase belongs to the burst's own instant. Taking it at log time, which
    // is when the burst ENDS, dates a burst at 995 ms into the next second.
    r.phase_ms = static_cast<uint16_t>(b.at_us / 1000 % 1000);
    r.event = event;
    r.rssi_dbm = b.rssi_dbm;
    r.len = b.len;
    r.bitrate = b.bitrate;
    std::memcpy(r.chips, b.chips, b.len);
    count_++;
}

bool Air::framed(const AirRecord& record, protocol::Frame& out) {
    uint8_t payload[protocol::kRxChipBytes] = {0};
    models::Sx1262::deliver_after_sync(record.chips, record.len, protocol::kSharedSync,
                                       protocol::kSharedSyncBits, payload, sizeof(payload));
    return protocol::receive_mband(payload, sizeof(payload), out);
}

bool Air::framed_uplink(const AirRecord& record, uint8_t* frame) {
    return models::Sx1262::deliver_after_sync(record.chips, record.len, protocol::kUplinkSync,
                                              protocol::kUplinkSyncBits, frame,
                                              protocol::kUplinkFrameBytes) != 0;
}

const AirRecord& Air::record(int i) const {
    const int first = count_ <= kLogSize ? 0 : count_ % kLogSize;
    return log_[(first + (i < 0 ? 0 : i)) % kLogSize];
}

void Air::clear() {
    for (Burst& b : burst_) b.used = false;
    count_ = 0;
    heard_ = deaf_ = collisions_ = 0;
    tx_in_flight_ = false;
}

// One line per burst, framed and decoded the way a receiver on the shared sync
// window would: name the system, then read what that system puts in the clear.
// ALP-TAS codes position against the receiver's own, which a channel log does
// not have, so it shows the address the frame carries unencrypted.
int Air::format(int i, char* out, int cap) const {
    if (i < 0 || i >= record_count()) {
        if (cap > 0) out[0] = 0;
        return 0;
    }
    const AirRecord& r = record(i);
    const int head = std::snprintf(out, static_cast<size_t>(cap), "%4u ms %s %s %3d dBm %3u B ",
                                   r.phase_ms, channel_name(r.freq_hz),
                                   kEventName[static_cast<int>(r.event)], r.rssi_dbm, r.len);

    const size_t left = static_cast<size_t>(cap - head);

    // The O band carries one thing and it is not Manchester-coded, so it is read
    // its own way: how many aircraft the ground station put in the frame, which
    // is the number a relay is worth judging by.
    if (tuned_to(r.freq_hz, timing::kObandHz)) {
        uint8_t frame_bytes[protocol::kUplinkFrameBytes] = {0};
        if (!framed_uplink(r, frame_bytes))
            return head + std::snprintf(out + head, left, "unframed");
        protocol::AdslUplink codec;
        messages::AircraftObs relayed[protocol::AdslUplink::kMaxTargets];
        protocol::AdslUplink::DecodeStats stats{};
        if (codec.decode(frame_bytes, relayed, protocol::AdslUplink::kMaxTargets, stats) !=
            Status::Ok)
            return head + std::snprintf(out + head, left, "UPLINK RS BAD");
        return head + std::snprintf(out + head, left, "UPLINK %d aircraft, %d corrected",
                                    stats.targets, stats.corrected);
    }

    protocol::Frame frame{};
    if (!framed(r, frame)) return head + std::snprintf(out + head, left, "unframed");

    if (frame.system == protocol::System::Alptas) {
        const uint32_t addr = protocol::alptas_address(frame.data);
        const char* crc = protocol::alptas_crc_ok(frame.data) ? "crc ok" : "CRC BAD";
        return head + std::snprintf(out + head, left, "ALP-TAS %06X %s", addr, crc);
    }

    protocol::AdslPacket p{};
    p.init();
    std::memcpy(&p.Version, frame.data, protocol::kAdslFrameBytes);
    const bool crc_ok = p.check_crc() == 0;
    p.descramble();
    return head + std::snprintf(out + head, left, "ADS-L %06X %+.5f,%+.5f %5d m %s", p.address(),
                                p.lat_1e7() / 1e7, p.lon_1e7() / 1e7, p.alt_m(),
                                crc_ok ? "crc ok" : "CRC BAD");
}

}  // namespace skyblip::simulator
