#include "core/settings/settings.h"

#include <cstring>

#include "core/fec/crc.h"
#include "core/util/json_min.h"

namespace skyblip::settings {

Settings defaults(uint32_t addr) {
    Settings s;
    s.device_addr = addr & 0x00FFFFFF;
    return s;
}

Status validate(const Settings& s) {
    if (s.version != Settings::kCurrentVersion) return Status::Unsupported;
    if (s.device_addr > 0x00FFFFFF) return Status::OutOfRange;
    if (s.addr_table > 63) return Status::OutOfRange;
    if (s.aircraft_type > 17) return Status::OutOfRange;
    if (s.alarm_volume > 5) return Status::OutOfRange;
    if (s.rotation > 3) return Status::OutOfRange;
    return Status::Ok;
}

namespace {
constexpr size_t kPayload = sizeof(Settings);
}

size_t blob_size() { return 1 + kPayload + 4; }

void to_blob(const Settings& s, uint8_t* out, size_t cap) {
    if (cap < blob_size()) return;
    out[0] = Settings::kCurrentVersion;
    std::memcpy(out + 1, &s, kPayload);
    uint32_t crc = fec::crc32(out, 1 + kPayload);
    out[1 + kPayload + 0] = static_cast<uint8_t>(crc);
    out[1 + kPayload + 1] = static_cast<uint8_t>(crc >> 8);
    out[1 + kPayload + 2] = static_cast<uint8_t>(crc >> 16);
    out[1 + kPayload + 3] = static_cast<uint8_t>(crc >> 24);
}

Status from_blob(const uint8_t* in, size_t len, Settings& out) {
    if (len < blob_size()) return Status::Crc;
    uint32_t crc = fec::crc32(in, 1 + kPayload);
    uint32_t stored = static_cast<uint32_t>(in[1 + kPayload]) |
                      (static_cast<uint32_t>(in[1 + kPayload + 1]) << 8) |
                      (static_cast<uint32_t>(in[1 + kPayload + 2]) << 16) |
                      (static_cast<uint32_t>(in[1 + kPayload + 3]) << 24);
    if (crc != stored) return Status::Crc;
    if (in[0] != Settings::kCurrentVersion) return Status::Unsupported;
    std::memcpy(&out, in + 1, kPayload);
    if (validate(out) != Status::Ok) return Status::Invalid;
    return Status::Ok;
}

int to_json(const Settings& s, char* buf, int cap) {
    json::Writer w(buf, cap);
    w.kv_int("version", s.version);
    w.kv_int("addr", static_cast<long>(s.device_addr));
    w.kv_int("addr_table", s.addr_table);
    w.kv_int("aircraft_type", s.aircraft_type);
    w.kv_int("region", s.region);
    w.kv_bool("alarm", s.alarm_enabled);
    w.kv_int("alarm_volume", s.alarm_volume);
    w.kv_bool("stealth", s.stealth);
    w.kv_int("units", static_cast<long>(s.units));
    w.kv_int("rotation", s.rotation);
    w.kv_int("page_mask", s.page_mask);
    w.kv_bool("power_save", s.power_save);
    w.kv_str("callsign", s.callsign);
    return w.finish();
}

Status apply_json(Settings& s, const char* json, int len) {
    json::Reader r(json, len);
    long v;
    bool b;
    Settings n = s;
    if (r.get_int("addr", v)) n.device_addr = static_cast<uint32_t>(v) & 0x00FFFFFF;
    if (r.get_int("addr_table", v)) n.addr_table = static_cast<uint8_t>(v);
    if (r.get_int("aircraft_type", v)) n.aircraft_type = static_cast<uint8_t>(v);
    if (r.get_int("region", v)) n.region = static_cast<uint8_t>(v);
    if (r.get_bool("alarm", b)) n.alarm_enabled = b;
    if (r.get_int("alarm_volume", v)) n.alarm_volume = static_cast<uint8_t>(v);
    if (r.get_bool("stealth", b)) n.stealth = b;
    if (r.get_int("units", v)) n.units = v ? Units::Imperial : Units::Metric;
    if (r.get_int("rotation", v)) n.rotation = static_cast<uint8_t>(v);
    if (r.get_int("page_mask", v)) n.page_mask = static_cast<uint8_t>(v);
    if (r.get_bool("power_save", b)) n.power_save = b;
    r.get_str("callsign", n.callsign, sizeof(n.callsign));
    Status st = validate(n);
    if (st != Status::Ok) return st;
    s = n;
    return Status::Ok;
}

}
