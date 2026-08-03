#include "core/flight/log_record.h"

#include "core/fec/crc.h"

namespace skyblip::flight {

namespace {

constexpr uint32_t kMaxTimeOffsetS = 0xFFFF;
constexpr uint16_t kHdopTenthsCeiling = 255;

void put_u16(uint8_t* out, uint16_t v) {
    out[0] = static_cast<uint8_t>(v);
    out[1] = static_cast<uint8_t>(v >> 8);
}

void put_u32(uint8_t* out, uint32_t v) {
    out[0] = static_cast<uint8_t>(v);
    out[1] = static_cast<uint8_t>(v >> 8);
    out[2] = static_cast<uint8_t>(v >> 16);
    out[3] = static_cast<uint8_t>(v >> 24);
}

uint16_t get_u16(const uint8_t* raw) {
    return static_cast<uint16_t>(raw[0] | (static_cast<uint16_t>(raw[1]) << 8));
}

uint32_t get_u32(const uint8_t* raw) {
    return static_cast<uint32_t>(raw[0]) | (static_cast<uint32_t>(raw[1]) << 8) |
           (static_cast<uint32_t>(raw[2]) << 16) | (static_cast<uint32_t>(raw[3]) << 24);
}

int16_t clamp_i16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return static_cast<int16_t>(v);
}

int8_t clamp_i8(int32_t v) {
    if (v > 127) return 127;
    if (v < -128) return -128;
    return static_cast<int8_t>(v);
}

}  // namespace

LogRecord log_record_from(const messages::OwnState& own) {
    LogRecord r{};
    r.utc = own.utc;
    r.lat_1e7 = own.lat_1e7;
    r.lon_1e7 = own.lon_1e7;
    r.alt_msl_m = own.alt_msl_m;
    r.alt_hae_m = own.alt_m;
    r.speed_q = own.speed_q;
    r.track_c9 = own.track_c9;
    r.climb_e8 = own.climb_e8;
    r.hdop_e2 = own.hdop_e2;
    r.sats = own.sats;
    r.flight_state = own.flight_state;
    r.fix_valid = own.fix_valid;
    r.utc_valid = own.utc_valid;
    r.pps_locked = own.pps_locked;
    r.climb_valid = own.climb_valid;
    r.geoid_separation_measured = own.geoid_separation_measured;
    return r;
}

void encode_log_record(const LogRecord& record, uint32_t base_utc, uint8_t* out) {
    const uint32_t elapsed = record.utc > base_utc ? record.utc - base_utc : 0;
    put_u16(out + 0, static_cast<uint16_t>(elapsed > kMaxTimeOffsetS ? kMaxTimeOffsetS : elapsed));
    put_u32(out + 2, static_cast<uint32_t>(record.lat_1e7));
    put_u32(out + 6, static_cast<uint32_t>(record.lon_1e7));
    put_u16(out + 10, static_cast<uint16_t>(clamp_i16(record.alt_msl_m)));
    // Height above the ellipsoid is stored as its distance from mean sea level:
    // EGM96 runs -107 m to +85 m over the whole planet, so one signed byte
    // carries the pair that two four-byte altitudes would have cost.
    out[12] = static_cast<uint8_t>(clamp_i8(record.alt_hae_m - record.alt_msl_m));
    put_u16(out + 13, record.speed_q);
    put_u16(out + 15, record.track_c9);
    put_u16(out + 17, static_cast<uint16_t>(record.climb_e8));
    out[19] = record.sats;
    const uint32_t tenths = (record.hdop_e2 + 5u) / 10u;
    out[20] = static_cast<uint8_t>(tenths > kHdopTenthsCeiling ? kHdopTenthsCeiling : tenths);

    uint8_t flags = 0;
    if (record.fix_valid) flags |= kLogFlagFixValid;
    if (record.utc_valid) flags |= kLogFlagUtcValid;
    if (record.pps_locked) flags |= kLogFlagPpsLocked;
    if (record.climb_valid) flags |= kLogFlagClimbValid;
    if (record.geoid_separation_measured) flags |= kLogFlagGeoidMeasured;
    if (record.session_end) flags |= kLogFlagSessionEnd;
    flags |= static_cast<uint8_t>((record.flight_state & kLogFlagFlightStateMask)
                                  << kLogFlagFlightStateShift);
    out[21] = flags;
    put_u16(out + 22, fec::crc16_ccitt(out, kLogRecordBytes - 2));
}

bool log_slot_erased(const uint8_t* raw, uint32_t len) {
    for (uint32_t i = 0; i < len; i++)
        if (raw[i] != 0xFF) return false;
    return true;
}

Status decode_log_record(const uint8_t* raw, uint32_t base_utc, LogRecord& out) {
    if (log_slot_erased(raw, kLogRecordBytes)) return Status::Empty;
    if (fec::crc16_ccitt(raw, kLogRecordBytes - 2) != get_u16(raw + 22)) return Status::Crc;

    out = LogRecord{};
    out.utc = base_utc + get_u16(raw + 0);
    out.lat_1e7 = static_cast<int32_t>(get_u32(raw + 2));
    out.lon_1e7 = static_cast<int32_t>(get_u32(raw + 6));
    out.alt_msl_m = static_cast<int16_t>(get_u16(raw + 10));
    out.alt_hae_m = out.alt_msl_m + static_cast<int8_t>(raw[12]);
    out.speed_q = get_u16(raw + 13);
    out.track_c9 = get_u16(raw + 15);
    out.climb_e8 = static_cast<int16_t>(get_u16(raw + 17));
    out.sats = raw[19];
    out.hdop_e2 = static_cast<uint16_t>(raw[20] * 10u);

    const uint8_t flags = raw[21];
    out.fix_valid = (flags & kLogFlagFixValid) != 0;
    out.utc_valid = (flags & kLogFlagUtcValid) != 0;
    out.pps_locked = (flags & kLogFlagPpsLocked) != 0;
    out.climb_valid = (flags & kLogFlagClimbValid) != 0;
    out.geoid_separation_measured = (flags & kLogFlagGeoidMeasured) != 0;
    out.session_end = (flags & kLogFlagSessionEnd) != 0;
    out.flight_state =
        static_cast<uint8_t>((flags >> kLogFlagFlightStateShift) & kLogFlagFlightStateMask);
    return Status::Ok;
}

void encode_log_sector_header(const LogSectorHeader& header, uint8_t* out) {
    put_u16(out + 0, kLogMagic);
    out[2] = header.version;
    // Written down rather than assumed: a later codec can grow the record and a
    // reader still knows how to walk the sectors this one left behind.
    out[3] = header.record_bytes;
    put_u32(out + 4, header.sequence);
    put_u32(out + 8, header.session_id);
    put_u16(out + 12, fec::crc16_ccitt(out, 12));
    put_u16(out + 14, 0xFFFF);
}

Status decode_log_sector_header(const uint8_t* raw, LogSectorHeader& out) {
    if (log_slot_erased(raw, kLogSectorHeaderBytes)) return Status::Empty;
    if (get_u16(raw + 0) != kLogMagic) return Status::Invalid;
    if (fec::crc16_ccitt(raw, 12) != get_u16(raw + 12)) return Status::Crc;
    out = LogSectorHeader{};
    out.version = raw[2];
    out.record_bytes = raw[3];
    out.sequence = get_u32(raw + 4);
    out.session_id = get_u32(raw + 8);
    if (out.version != kLogVersion || out.record_bytes != kLogRecordBytes)
        return Status::Unsupported;
    return Status::Ok;
}

}  // namespace skyblip::flight
