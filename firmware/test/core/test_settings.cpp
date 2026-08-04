// Settings survive a power cycle, so every way a stored blob can be wrong is a way
// the device can come up misconfigured: corrupted, written by an older firmware,
// or patched by a client sending a value out of range. Each one falls back or
// refuses whole. A partly applied patch is the outcome none of these allow.
//
// The address cases are the other half of the same subject: what the device says
// it is. A chip id is a serial number, and the space it lands in is shared with
// every other tracker that mints its identity the same way.
#include <cstring>
#include <string>

#include "core/fec/crc.h"
#include "core/power/battery.h"
#include "core/settings/address.h"
#include "core/settings/settings.h"
#include "core/util/json_min.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::settings;

TEST_CASE("settings: defaults are valid") {
    Settings s = defaults(0x123456);
    CHECK(validate(s) == Status::Ok);
    CHECK(s.device_addr == 0x123456u);
    CHECK(int(s.aircraft_type) == 4);
}

TEST_CASE("settings: blob round-trips through version+crc framing") {
    Settings s = defaults(0xABCDEF);
    s.alarm_volume = 4;
    s.stealth = true;
    uint8_t blob[128];
    to_blob(s, blob, sizeof(blob));
    Settings out;
    CHECK(from_blob(blob, blob_size(), out) == Status::Ok);
    CHECK(out.device_addr == 0xABCDEFu);
    CHECK(int(out.alarm_volume) == 4);
    CHECK(out.stealth);
}

TEST_CASE("settings: a corrupted blob is detected (CRC), caller falls back") {
    Settings s = defaults(1);
    uint8_t blob[128];
    to_blob(s, blob, sizeof(blob));
    blob[3] ^= 0xFF;  // flip a payload byte
    Settings out;
    CHECK(from_blob(blob, blob_size(), out) == Status::Crc);
}

TEST_CASE("settings: wrong version blob is Unsupported (migrate/default)") {
    Settings s = defaults(1);
    uint8_t blob[128];
    to_blob(s, blob, sizeof(blob));
    blob[0] = 99;  // bogus version
    // recompute crc so it isn't a CRC failure: simulate a real older version
    // (here we just confirm version mismatch is caught before trusting payload)
    Settings out;
    Status st = from_blob(blob, blob_size(), out);
    CHECK((st == Status::Unsupported || st == Status::Crc));
}

TEST_CASE("settings: to_json/apply_json round-trip of a patch") {
    Settings s = defaults(0x010203);
    char buf[256];
    int n = to_json(s, buf, sizeof(buf));
    CHECK(n > 0);
    json::Reader r(buf, n);
    long v;
    CHECK(r.get_int("aircraft_type", v));
    CHECK(v == 4);

    // apply a patch: change type + alarm volume + stealth
    const char* patch = "{\"aircraft_type\":1,\"alarm_volume\":5,\"stealth\":true}";
    CHECK(apply_json(s, patch, static_cast<int>(strlen(patch))) == Status::Ok);
    CHECK(int(s.aircraft_type) == 1);
    CHECK(int(s.alarm_volume) == 5);
    CHECK(s.stealth);
}

TEST_CASE("settings: apply_json rejects out-of-range atomically") {
    Settings s = defaults(1);
    uint8_t before = s.aircraft_type;
    const char* bad = "{\"aircraft_type\":99}";  // > 17
    CHECK(apply_json(s, bad, static_cast<int>(strlen(bad))) == Status::OutOfRange);
    CHECK(s.aircraft_type == before);  // unchanged (atomic)
}

// F4. Address hygiene: the prefixes SoftRF moves off
// (oss/SoftRF-lyusupov .../src/system/SoC.cpp:83-110), judged against what our
// own address table already says.
TEST_CASE("address: a self-minted address leaves the prefixes other trackers crowd") {
    // 0xD0, 0xDD, 0xDE and 0xDF are the congested FLARM range; 0x11 is Skytraxx.
    for (uint32_t prefix : {0xD0u, 0xDDu, 0xDEu, 0xDFu, 0x11u}) {
        for (uint32_t low = 0; low <= 0xFFFF; low += 0x111) {
            const uint32_t raw = (prefix << 16) | low;
            const uint32_t safe = safe_device_address(raw);
            CHECK_FALSE(address_is_crowded(safe));
            CHECK((safe & 0xFFFFu) == low);            // only the prefix moves
            CHECK(safe <= kAddressMask);               // and it stays inside 24 bits
            CHECK(safe_device_address(safe) == safe);  // applying it twice is the same answer
        }
    }
}

TEST_CASE("address: a prefix nobody crowds is left exactly where the chip put it") {
    for (uint32_t prefix = 0; prefix <= 0xFF; prefix++) {
        const uint32_t raw = (prefix << 16) | 0xABCD;
        const uint32_t safe = safe_device_address(raw);
        if (address_is_crowded(raw))
            CHECK(safe != raw);
        else
            CHECK(safe == raw);
    }
    // 0x5B is NOT dodged: SoftRF avoids it for an OGN 0.2.8 decoder bug in the
    // legacy 'Air V6' frame, which this firmware never transmits.
    CHECK(safe_device_address(0x5B1234) == 0x5B1234u);
}

TEST_CASE("address: an address that is not a device's to mint is transmitted as issued") {
    // Table 5 is ICAO, 6 FLARM, 7 OGN: those were issued to the aircraft, prefix
    // and all. Tables 0 to 4 are self-minted, and only those may be moved.
    CHECK(safe_air_address(0xDD1234, 5) == 0xDD1234u);
    CHECK(safe_air_address(0xDD1234, 6) == 0xDD1234u);
    CHECK(safe_air_address(0x111111, 7) == 0x111111u);
    CHECK(safe_air_address(0xDD1234, 0) == 0xED1234u);
    CHECK(safe_air_address(0x111111, 4) == 0x121111u);
}

TEST_CASE("address: neither all-zeros nor all-ones goes on the air") {
    // 0x000000 is 'no address' to every decoder, 0xFFFFFF is what a dead read
    // produces. A chip id landing on either is answered with a fixed address.
    CHECK(safe_device_address(0x000000) == kFallbackAddress);
    CHECK(safe_device_address(0xFFFFFF) == kFallbackAddress);
    CHECK(safe_device_address(0xFF000000) == kFallbackAddress);  // masked to 24 bits first
    CHECK(defaults(0).device_addr == kFallbackAddress);
}

TEST_CASE("settings: the defaults a chip id produces are already hygienic") {
    CHECK(defaults(0xDD0042).device_addr == 0xED0042u);
    CHECK(validate(defaults(0xDD0042)) == Status::Ok);
}

// B4. A stored blob is a data format: deleting a field is expand, migrate,
// contract, and the migration is the part a device in the field depends on.
TEST_CASE("settings: a blob written by version-1 firmware comes back as itself") {
    // The version-1 payload, byte for byte as that firmware memcpy'd its struct:
    // region, rotation and power_save sat between aircraft_type and callsign.
    struct V1 {
        uint8_t version{1};
        uint32_t device_addr{0};
        uint8_t addr_table{0};
        uint8_t aircraft_type{4};
        uint8_t region{0};
        bool alarm_enabled{true};
        uint8_t alarm_volume{3};
        bool stealth{false};
        Units units{Units::Metric};
        uint8_t rotation{0};
        uint8_t page_mask{0x0F};
        bool power_save{false};
        char callsign[10]{0};
    };

    V1 old{};
    old.device_addr = 0x0ABBCC;
    old.addr_table = 6;
    old.aircraft_type = 9;
    old.region = 2;         // a field this firmware no longer has
    old.rotation = 3;       // and another
    old.power_save = true;  // and the third
    old.alarm_enabled = false;
    old.alarm_volume = 5;
    old.stealth = true;
    old.units = Units::Imperial;
    old.page_mask = 0x05;
    std::memcpy(old.callsign, "D-KXYZ", 7);

    uint8_t blob[128] = {0};
    blob[0] = 1;
    std::memcpy(blob + 1, &old, sizeof(V1));
    const uint32_t crc = fec::crc32(blob, 1 + sizeof(V1));
    for (int i = 0; i < 4; i++) blob[1 + sizeof(V1) + i] = static_cast<uint8_t>(crc >> (8 * i));

    Settings out;
    REQUIRE(from_blob(blob, 1 + sizeof(V1) + 4, out) == Status::Ok);
    CHECK(out.device_addr == 0x0ABBCCu);
    CHECK(int(out.addr_table) == 6);
    CHECK(int(out.aircraft_type) == 9);
    CHECK_FALSE(out.alarm_enabled);
    CHECK(int(out.alarm_volume) == 5);
    CHECK(out.stealth);
    CHECK(out.units == Units::Imperial);
    CHECK(int(out.page_mask) == 0x05);
    CHECK(std::string(out.callsign) == "D-KXYZ");
    CHECK(int(out.version) == int(Settings::kCurrentVersion));

    // And what it writes back is the current layout, not the one it read.
    uint8_t rewritten[128] = {0};
    to_blob(out, rewritten, sizeof(rewritten));
    CHECK(int(rewritten[0]) == int(kBlobVersion));
    Settings again;
    CHECK(from_blob(rewritten, blob_size(), again) == Status::Ok);
    CHECK(std::string(again.callsign) == "D-KXYZ");
    CHECK(again.stealth);
}

// H. The per-unit gauge trim: one signed millivolt offset, set once on the line
// against a bench supply, bounded because a calibration field that accepts
// anything is a support incident of its own.
TEST_CASE("settings: the battery trim is bounded at the boundary, in both framings") {
    Settings s = defaults(0x0ABBCC);
    CHECK(int(s.battery_offset_mv) == 0);  // an uncalibrated unit reads as it always did

    // The bound itself is inclusive, and one millivolt past it is not.
    s.battery_offset_mv = power::kCalibrationLimitMv;
    CHECK(validate(s) == Status::Ok);
    s.battery_offset_mv = -power::kCalibrationLimitMv;
    CHECK(validate(s) == Status::Ok);
    s.battery_offset_mv = power::kCalibrationLimitMv + 1;
    CHECK(validate(s) == Status::OutOfRange);
    s.battery_offset_mv = -(power::kCalibrationLimitMv + 1);
    CHECK(validate(s) == Status::OutOfRange);

    // A blob is the other way in, and it is validated on the way out of flash:
    // a unit whose stored trim is impossible falls back rather than reading its
    // cell through it.
    s.battery_offset_mv = -47;
    uint8_t blob[128] = {0};
    to_blob(s, blob, sizeof(blob));
    Settings out;
    REQUIRE(from_blob(blob, blob_size(), out) == Status::Ok);
    CHECK(int(out.battery_offset_mv) == -47);

    Settings impossible = s;
    impossible.battery_offset_mv = 3000;
    to_blob(impossible, blob, sizeof(blob));
    CHECK(from_blob(blob, blob_size(), out) == Status::Invalid);
}

TEST_CASE("settings: the battery trim is set over the link and refused whole when it is not") {
    Settings s = defaults(1);
    const char* trim = "{\"battery_offset_mv\":-40}";
    CHECK(apply_json(s, trim, static_cast<int>(strlen(trim))) == Status::Ok);
    CHECK(int(s.battery_offset_mv) == -40);

    // Out of bound, and the rest of the patch does not land either.
    const char* far_out = "{\"alarm_volume\":1,\"battery_offset_mv\":900}";
    CHECK(apply_json(s, far_out, static_cast<int>(strlen(far_out))) == Status::OutOfRange);
    CHECK(int(s.battery_offset_mv) == -40);
    CHECK(int(s.alarm_volume) == 3);

    // The boundary a narrowing cast would hide: 65536 truncates to 0 in an
    // int16, and 65576 truncates to 40 - both inside the bound, neither what
    // the client sent. The check has to happen before the narrowing.
    const char* wraps_to_zero = "{\"battery_offset_mv\":65536}";
    CHECK(apply_json(s, wraps_to_zero, static_cast<int>(strlen(wraps_to_zero))) ==
          Status::OutOfRange);
    CHECK(int(s.battery_offset_mv) == -40);
    const char* wraps_into_range = "{\"battery_offset_mv\":65576}";
    CHECK(apply_json(s, wraps_into_range, static_cast<int>(strlen(wraps_into_range))) ==
          Status::OutOfRange);
    CHECK(int(s.battery_offset_mv) == -40);
}

// H, the migration. The battery trim went in beside the address rather than at
// the end, which costs no padding, so version 3 is the same LENGTH as version 2
// and a different layout. A length check cannot tell them apart and the version
// byte must: read the old bytes as the new struct and a page_mask of 15 becomes
// a callsign, an aircraft type becomes a trim of several hundred millivolts.
TEST_CASE("settings: a blob written by version-2 firmware comes back as itself, untrimmed") {
    // The version-2 payload, byte for byte as that firmware memcpy'd its struct.
    struct V2 {
        uint8_t version{1};
        uint32_t device_addr{0};
        uint8_t addr_table{0};
        uint8_t aircraft_type{4};
        bool alarm_enabled{true};
        uint8_t alarm_volume{3};
        bool stealth{false};
        Units units{Units::Metric};
        uint8_t page_mask{0x0F};
        char callsign[10]{0};
    };

    V2 old{};
    old.device_addr = 0x0ABBCC;
    old.addr_table = 6;
    old.aircraft_type = 9;
    old.alarm_enabled = false;
    old.alarm_volume = 5;
    old.stealth = true;
    old.units = Units::Imperial;
    old.page_mask = 0x05;
    std::memcpy(old.callsign, "D-KXYZ", 7);

    uint8_t blob[128] = {0};
    blob[0] = 2;
    std::memcpy(blob + 1, &old, sizeof(V2));
    const uint32_t crc = fec::crc32(blob, 1 + sizeof(V2));
    for (int i = 0; i < 4; i++) blob[1 + sizeof(V2) + i] = static_cast<uint8_t>(crc >> (8 * i));

    Settings out;
    REQUIRE(from_blob(blob, 1 + sizeof(V2) + 4, out) == Status::Ok);
    CHECK(out.device_addr == 0x0ABBCCu);
    CHECK(int(out.addr_table) == 6);
    CHECK(int(out.aircraft_type) == 9);
    CHECK_FALSE(out.alarm_enabled);
    CHECK(int(out.alarm_volume) == 5);
    CHECK(out.stealth);
    CHECK(out.units == Units::Imperial);
    CHECK(int(out.page_mask) == 0x05);
    CHECK(std::string(out.callsign) == "D-KXYZ");
    // A unit that stored its settings before the trim existed was never
    // calibrated, so it comes back reading exactly as it did yesterday.
    CHECK(int(out.battery_offset_mv) == 0);

    // And what it writes back is the current layout, which the current firmware
    // reads and the old blob's version byte no longer claims.
    uint8_t rewritten[128] = {0};
    to_blob(out, rewritten, sizeof(rewritten));
    CHECK(int(rewritten[0]) == int(kBlobVersion));
    CHECK(int(kBlobVersion) == 4);
}

// J, the migration. Version 4 added the radio's frequency trim; the two 16-bit
// per-unit trims now fill one word between the address and the byte fields, so
// this blob is four bytes SHORTER than the current one rather than the same
// length version 2 and 3 shared. Both mistakes are refused by the same reader.
TEST_CASE("settings: a blob written by version-3 firmware comes back as itself, untrimmed") {
    // The version-3 payload, byte for byte as that firmware memcpy'd its struct.
    struct V3 {
        uint8_t version{1};
        uint32_t device_addr{0};
        int16_t battery_offset_mv{0};
        uint8_t addr_table{0};
        uint8_t aircraft_type{4};
        bool alarm_enabled{true};
        uint8_t alarm_volume{3};
        bool stealth{false};
        Units units{Units::Metric};
        uint8_t page_mask{0x0F};
        char callsign[10]{0};
    };
    CHECK(sizeof(V3) + 4 == blob_size() - 5);

    V3 old{};
    old.device_addr = 0x0ABBCC;
    old.battery_offset_mv = -120;  // a unit that WAS calibrated keeps its trim
    old.addr_table = 6;
    old.aircraft_type = 9;
    old.alarm_enabled = false;
    old.alarm_volume = 5;
    old.stealth = true;
    old.units = Units::Imperial;
    old.page_mask = 0x05;
    std::memcpy(old.callsign, "D-KXYZ", 7);

    uint8_t blob[128] = {0};
    blob[0] = 3;
    std::memcpy(blob + 1, &old, sizeof(V3));
    const uint32_t crc = fec::crc32(blob, 1 + sizeof(V3));
    for (int i = 0; i < 4; i++) blob[1 + sizeof(V3) + i] = static_cast<uint8_t>(crc >> (8 * i));

    Settings out;
    REQUIRE(from_blob(blob, 1 + sizeof(V3) + 4, out) == Status::Ok);
    CHECK(out.device_addr == 0x0ABBCCu);
    CHECK(int(out.battery_offset_mv) == -120);
    CHECK(int(out.addr_table) == 6);
    CHECK(int(out.aircraft_type) == 9);
    CHECK_FALSE(out.alarm_enabled);
    CHECK(int(out.alarm_volume) == 5);
    CHECK(out.stealth);
    CHECK(out.units == Units::Imperial);
    CHECK(int(out.page_mask) == 0x05);
    CHECK(std::string(out.callsign) == "D-KXYZ");
    // A unit that stored its settings before the radio trim existed was never
    // measured, so it comes back on the reference its TCXO actually has: the
    // frequency it has been transmitting on all along.
    CHECK(int(out.freq_trim_e1_ppm) == 0);

    uint8_t rewritten[128] = {0};
    to_blob(out, rewritten, sizeof(rewritten));
    CHECK(int(rewritten[0]) == int(kBlobVersion));
    Settings again;
    REQUIRE(from_blob(rewritten, blob_size(), again) == Status::Ok);
    CHECK(int(again.battery_offset_mv) == -120);
    CHECK(int(again.freq_trim_e1_ppm) == 0);
}

// J. The frequency trim: the field exists because a TCXO gives no way to find
// out it is wrong, so the bound is what says "out of trim" rather than "broken".
TEST_CASE("settings: the frequency trim is bounded at the boundary, in both framings") {
    Settings s = defaults(0x0ABBCC);
    CHECK(int(s.freq_trim_e1_ppm) == 0);  // the design intent on a TCXO part

    s.freq_trim_e1_ppm = kFreqTrimLimitTenthsPpm;
    CHECK(validate(s) == Status::Ok);
    s.freq_trim_e1_ppm = -kFreqTrimLimitTenthsPpm;
    CHECK(validate(s) == Status::Ok);
    s.freq_trim_e1_ppm = kFreqTrimLimitTenthsPpm + 1;
    CHECK(validate(s) == Status::OutOfRange);
    s.freq_trim_e1_ppm = -(kFreqTrimLimitTenthsPpm + 1);
    CHECK(validate(s) == Status::OutOfRange);

    // And it survives the flash framing, sign and all: a stored trim that came
    // back as its own negation would move the carrier twice as far the wrong way.
    s.freq_trim_e1_ppm = -37;
    REQUIRE(validate(s) == Status::Ok);
    uint8_t blob[128] = {0};
    to_blob(s, blob, sizeof(blob));
    Settings out;
    REQUIRE(from_blob(blob, blob_size(), out) == Status::Ok);
    CHECK(int(out.freq_trim_e1_ppm) == -37);
}

TEST_CASE("settings: the frequency trim is set over the link and refused whole when it is not") {
    Settings s = defaults(0x0ABBCC);
    const char* set = "{\"freq_trim_e1_ppm\":-25,\"alarm_volume\":2}";
    REQUIRE(apply_json(s, set, static_cast<int>(strlen(set))) == Status::Ok);
    CHECK(int(s.freq_trim_e1_ppm) == -25);
    CHECK(int(s.alarm_volume) == 2);

    // Out of range is refused as a whole patch, not clamped and not partly
    // applied: a bench that asked for 500 ppm has the wrong number written down,
    // and silently storing 10 ppm instead would hide that.
    const char* far = "{\"freq_trim_e1_ppm\":5000,\"alarm_volume\":5}";
    CHECK(apply_json(s, far, static_cast<int>(strlen(far))) == Status::OutOfRange);
    CHECK(int(s.freq_trim_e1_ppm) == -25);
    CHECK(int(s.alarm_volume) == 2);

    // Narrowed before it is validated: 65536 tenths of a ppm truncates to 0 in
    // an int16 and would pass a bound check that never saw the value sent.
    const char* wrapped = "{\"freq_trim_e1_ppm\":65536}";
    CHECK(apply_json(s, wrapped, static_cast<int>(strlen(wrapped))) == Status::OutOfRange);
    CHECK(int(s.freq_trim_e1_ppm) == -25);

    // Write-only over the link, for the same reason battery_offset_mv is: the
    // "get" reply is one frame at comms::kSmallestSupportedPayload with nine
    // bytes of headroom, and "freq_trim_e1_ppm" alone is nineteen.
    char buf[256];
    const int n = to_json(s, buf, sizeof(buf));
    CHECK(std::string(buf, static_cast<size_t>(n)).find("freq_trim") == std::string::npos);
}

TEST_CASE("settings: a blob from a version this firmware never wrote is refused") {
    Settings s = defaults(0x0ABBCC);
    uint8_t blob[128];
    to_blob(s, blob, sizeof(blob));
    blob[0] = 7;
    Settings out;
    CHECK(from_blob(blob, blob_size(), out) == Status::Unsupported);
}

TEST_CASE("settings: the JSON offers nothing the firmware does not read") {
    Settings s = defaults(0x0ABBCC);
    char buf[256];
    const int n = to_json(s, buf, sizeof(buf));
    const std::string json(buf, static_cast<size_t>(n));
    // Removed with their fields: no reader outside this module, and a page that
    // accepts a setting nothing reads is worse than one that does not offer it.
    CHECK(json.find("region") == std::string::npos);
    CHECK(json.find("rotation") == std::string::npos);
    CHECK(json.find("power_save") == std::string::npos);
    // Kept, because each of these is read: the air (stealth), the panel
    // (callsign, page_mask, units), the annunciator (alarm, alarm_volume).
    CHECK(json.find("stealth") != std::string::npos);
    CHECK(json.find("callsign") != std::string::npos);
    // units survived the same audit the three above failed, on one page: the
    // six-pack graduates its dials in km/h, metres and m/s or in kt, ft and
    // fpm, and there is nowhere else a value appears exactly once.
    CHECK(json.find("units") != std::string::npos);
    // The one exception, and it goes the other way: the firmware reads
    // battery_offset_mv and the reply does not offer it. The "config" reply is
    // one frame at comms::kSmallestSupportedPayload, its worst case is already
    // 173 of those 182 bytes, and no honest name for a signed millivolt trim
    // fits in nine. It is written on the line and read back as the millivolts
    // the status reply and the panel already show.
    CHECK(json.find("battery_offset_mv") == std::string::npos);
    const char* imperial = "{\"units\":1}";
    CHECK(apply_json(s, imperial, static_cast<int>(strlen(imperial))) == Status::Ok);
    CHECK(s.units == Units::Imperial);

    // An older client still sending the dropped keys is not an error: the rest
    // of its patch applies, and the keys nothing reads are ignored.
    const char* stale = "{\"region\":3,\"rotation\":2,\"power_save\":true,\"alarm_volume\":1}";
    CHECK(apply_json(s, stale, static_cast<int>(strlen(stale))) == Status::Ok);
    CHECK(int(s.alarm_volume) == 1);
}

TEST_CASE("settings: a callsign is what a panel can draw, and a patch that is not is refused") {
    Settings s = defaults(1);
    const char* ok = "{\"callsign\":\"G-ABCD\"}";
    CHECK(apply_json(s, ok, static_cast<int>(strlen(ok))) == Status::Ok);
    CHECK(std::string(s.callsign) == "G-ABCD");

    Settings bad = s;
    bad.callsign[2] = 0x07;  // a control character the 5x7 font has no glyph for
    CHECK(validate(bad) == Status::Invalid);

    // Nine characters plus the terminator is the whole field, and it survives.
    const char* full = "{\"callsign\":\"123456789\"}";
    CHECK(apply_json(s, full, static_cast<int>(strlen(full))) == Status::Ok);
    CHECK(std::string(s.callsign) == "123456789");
}

TEST_CASE("settings: a patched address is hygienic when it is ours to mint, kept when it is not") {
    Settings s = defaults(0x0ABBCC);
    const char* self_minted = "{\"addr\":11599823,\"addr_table\":0}";  // 0xB0FFCF -> untouched
    CHECK(apply_json(s, self_minted, static_cast<int>(strlen(self_minted))) == Status::Ok);
    CHECK(s.device_addr == 0xB0FFCFu);

    const char* crowded = "{\"addr\":14488116,\"addr_table\":0}";  // 0xDD1234
    CHECK(apply_json(s, crowded, static_cast<int>(strlen(crowded))) == Status::Ok);
    CHECK(s.device_addr == 0xED1234u);

    const char* icao = "{\"addr\":14488116,\"addr_table\":5}";
    CHECK(apply_json(s, icao, static_cast<int>(strlen(icao))) == Status::Ok);
    CHECK(s.device_addr == 0xDD1234u);
}

TEST_CASE("json_min: the reader parses ints, bools and strings, the writer emits them") {
    const char* j = "{\"a\":42,\"b\":true,\"c\":\"hi\",\"d\":-7}";
    json::Reader r(j, static_cast<int>(strlen(j)));
    long v;
    bool b;
    char s[8];
    CHECK(r.get_int("a", v));
    CHECK(v == 42);
    CHECK(r.get_bool("b", b));
    CHECK(b);
    CHECK(r.get_str("c", s, sizeof(s)));
    CHECK(std::string(s) == "hi");
    CHECK(r.get_int("d", v));
    CHECK(v == -7);
    CHECK_FALSE(r.has("z"));

    char out[64];
    json::Writer w(out, sizeof(out));
    w.kv_int("x", 5);
    w.kv_bool("y", false);
    w.finish();
    CHECK(std::string(out) == "{\"x\":5,\"y\":false}");
}

// core/comms's status reply is a fixed-size stack buffer with no heap behind
// it: a key that overruns it must never come out half-written. A writer that
// silently dropped the tail of its last key would still close the brace and
// look like valid, complete JSON to anything downstream.
TEST_CASE("json_min: a key that will not fit whole is left out, not cut short") {
    char out[16];
    json::Writer w(out, sizeof(out));
    w.kv_int("x", 5);
    CHECK_FALSE(w.overflowed());

    w.kv_bool("yy", true);  // does not fit in what is left of a 16-byte buffer
    CHECK(w.overflowed());

    const int n = w.finish();
    CHECK(std::string(out) == "{\"x\":5}");  // whole and valid, one key short
    CHECK(n == 7);
    CHECK(static_cast<int>(std::strlen(out)) == n);  // the reported length is real
}

TEST_CASE("json_min: a buffer sized for the exact worst case never overflows") {
    char out[29];
    json::Writer w(out, sizeof(out));
    w.kv_str("k", "say \"hi\"");  // 1-char key, two escaped quotes in the value
    w.kv_bool("b", false);
    const int n = w.finish();
    CHECK_FALSE(w.overflowed());
    CHECK(static_cast<int>(std::strlen(out)) == n);
    CHECK(std::string(out) == "{\"k\":\"say \\\"hi\\\"\",\"b\":false}");
}
