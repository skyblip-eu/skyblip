#include "core/protocol/adsl_uplink.h"

#include <cstring>

#include "core/fec/scramble.h"

namespace skyblip::protocol {

namespace {
void put_u24(uint8_t* b, uint32_t v) {
    b[0] = static_cast<uint8_t>(v);
    b[1] = static_cast<uint8_t>(v >> 8);
    b[2] = static_cast<uint8_t>(v >> 16);
}
uint32_t get_u24(const uint8_t* b) {
    return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
           (static_cast<uint32_t>(b[2]) << 16);
}
void put_i32(uint8_t* b, int32_t v) {
    uint32_t u = static_cast<uint32_t>(v);
    b[0] = static_cast<uint8_t>(u);
    b[1] = static_cast<uint8_t>(u >> 8);
    b[2] = static_cast<uint8_t>(u >> 16);
    b[3] = static_cast<uint8_t>(u >> 24);
}
int32_t get_i32(const uint8_t* b) {
    return static_cast<int32_t>(static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
                                (static_cast<uint32_t>(b[2]) << 16) |
                                (static_cast<uint32_t>(b[3]) << 24));
}
void put_i16(uint8_t* b, int16_t v) {
    uint16_t u = static_cast<uint16_t>(v);
    b[0] = static_cast<uint8_t>(u);
    b[1] = static_cast<uint8_t>(u >> 8);
}
int16_t get_i16(const uint8_t* b) {
    return static_cast<int16_t>(static_cast<uint16_t>(b[0]) | (static_cast<uint16_t>(b[1]) << 8));
}

void pack_record(uint8_t* r, const messages::AircraftObs& t) {
    put_u24(r, t.addr & 0x00FFFFFF);
    r[3] = static_cast<uint8_t>((t.addr_table & 0x3F) | ((t.flight_state & 0x03) << 6));
    r[4] = t.acft_cat;
    r[5] = static_cast<uint8_t>(t.speed_q > 255 ? 255 : t.speed_q);
    put_i32(r + 6, t.lat_1e7);
    put_i32(r + 10, t.lon_1e7);
    put_i16(r + 14, static_cast<int16_t>(t.alt_m));
}

void unpack_record(const uint8_t* r, messages::AircraftObs& t) {
    t = messages::AircraftObs{};
    t.addr = get_u24(r);
    t.addr_table = r[3] & 0x3F;
    t.flight_state = (r[3] >> 6) & 0x03;
    t.acft_cat = r[4];
    t.speed_q = r[5];
    t.has_speed = r[5] != 0;
    t.lat_1e7 = get_i32(r + 6);
    t.lon_1e7 = get_i32(r + 10);
    t.alt_m = get_i16(r + 14);
    t.valid_pos = true;
    t.source = messages::Source::AdslUplink;
    t.emergency = 1;
}
}

Status AdslUplink::encode(const messages::AircraftObs* targets, int n, uint8_t key_index,
                          uint8_t out_frame[kFrameBytes]) const {
    if (n < 0 || n > kMaxTargets) return Status::Full;
    uint8_t data[fec::ReedSolomon255::kK];
    std::memset(data, 0, sizeof(data));
    data[0] = kVersion;
    data[1] = static_cast<uint8_t>(n);
    data[2] = key_index;
    for (int i = 0; i < n; i++) {
        pack_record(data + kHeaderBytes + i * kRecordBytes, targets[i]);
    }
    uint32_t words[fec::ReedSolomon255::kK / 4];
    std::memcpy(words, data, sizeof(words));
    fec::xxtea_scramble_key0(words, sizeof(words) / 4, 6);
    std::memcpy(data, words, sizeof(words));

    std::memcpy(out_frame, data, fec::ReedSolomon255::kK);
    rs_.encode(data, out_frame + fec::ReedSolomon255::kK);
    return Status::Ok;
}

Status AdslUplink::decode(const uint8_t frame[kFrameBytes], messages::AircraftObs* targets, int cap,
                          DecodeStats& stats) const {
    uint8_t cw[fec::ReedSolomon255::kN];
    std::memcpy(cw, frame, sizeof(cw));
    int corrected = rs_.decode(cw);
    if (corrected < 0) return Status::Crc;
    stats.corrected = corrected;

    uint8_t data[fec::ReedSolomon255::kK];
    std::memcpy(data, cw, fec::ReedSolomon255::kK);
    uint32_t words[fec::ReedSolomon255::kK / 4];
    std::memcpy(words, data, sizeof(words));
    fec::xxtea_descramble_key0(words, sizeof(words) / 4, 6);
    std::memcpy(data, words, sizeof(words));

    if (data[0] != kVersion) return Status::Unsupported;
    int n = data[1];
    if (n > kMaxTargets) return Status::Invalid;
    int out = 0;
    for (int i = 0; i < n && out < cap; i++) {
        unpack_record(data + kHeaderBytes + i * kRecordBytes, targets[out++]);
    }
    stats.targets = out;
    return Status::Ok;
}

}
