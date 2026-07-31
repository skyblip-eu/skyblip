// ALP-TAS (FLARM-wire, 2024 protocol) air-frame codec. Two things are worth
// testing here and both are safety-critical: that a frame we transmit is the
// frame the protocol says (encode -> decode round trip, at several latitudes and
// across the 20-bit position wrap), and that a frame that is NOT ours is thrown
// away (frame CRC, plus the decrypt-plausibility gate that is the only thing
// standing between a mis-framed foreign packet and the traffic table).
#include <cstdlib>  // std::abs - libc++ pulls it in transitively, libstdc++ does not
#include <cstring>

#include "core/protocol/alptas.h"
#include "core/protocol/nmea_out.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::protocol;

namespace {
// Deterministic xorshift so any failure is reproducible in CI.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
};

constexpr uint32_t kUtc = 1700000000;  // 1700000000 % 16 == 0, so timebits == 0

messages::AircraftObs make_obs(int32_t lat_1e7, int32_t lon_1e7) {
    messages::AircraftObs obs{};
    obs.addr = 0x123456;
    obs.addr_table = 0x06;  // FLARM-issued address
    obs.aircraft_cat = 4;   // glider
    obs.flight_state = 2;   // airborne
    obs.lat_1e7 = lat_1e7;
    obs.lon_1e7 = lon_1e7;
    obs.alt_m = 1234;
    obs.speed_q = 45 * 4;  // 45 m/s
    obs.climb_e8 = -44;    // -5.5 m/s
    obs.track_c9 = 256;    // 180 deg
    obs.has_speed = true;
    obs.has_climb = true;
    obs.valid_pos = true;
    return obs;
}

// Longitude quantum in 1e-7 deg at this latitude, from the protocol's own table:
// this is the tolerance a longitude round trip is entitled to.
int32_t lon_quantum(int32_t lat_1e7) {
    int32_t deg = std::abs(lat_1e7) / 10000000;
    static const int32_t kStep[] = {52, 65, 80, 101, 129, 174, 252, 362, 552, 806};
    if (deg >= 89) return kStep[9];
    if (deg >= 84) return kStep[8];
    if (deg >= 79) return kStep[7];
    if (deg >= 74) return kStep[6];
    if (deg >= 69) return kStep[5];
    if (deg >= 64) return kStep[4];
    if (deg >= 54) return kStep[3];
    if (deg >= 39) return kStep[2];
    if (deg >= 26) return kStep[1];
    return 806;  // never tighter than the coarsest step; latitudes above are exact
}

void check_position_round_trip(int32_t lat_1e7, int32_t lon_1e7, int32_t ref_lat_1e7,
                               int32_t ref_lon_1e7) {
    CAPTURE(lat_1e7);
    CAPTURE(lon_1e7);
    messages::AircraftObs obs = make_obs(lat_1e7, lon_1e7);
    uint8_t frame[kAlptasFrameBytes];
    REQUIRE(alptas_encode(frame, obs, kUtc, ref_lat_1e7, ref_lon_1e7) == Status::Ok);
    messages::AircraftObs got{};
    REQUIRE(alptas_decode(frame, kUtc, ref_lat_1e7, ref_lon_1e7, got) == Status::Ok);
    CHECK(std::abs(got.lat_1e7 - lat_1e7) <= 26);
    CHECK(std::abs(got.lon_1e7 - lon_1e7) <= lon_quantum(lat_1e7));
}
}  // namespace

TEST_CASE("alptas: encode then decode preserves the identity and motion fields") {
    messages::AircraftObs obs = make_obs(481234567, 87654321);
    uint8_t frame[kAlptasFrameBytes];
    REQUIRE(alptas_encode(frame, obs, kUtc, 480000000, 87000000) == Status::Ok);

    messages::AircraftObs got{};
    REQUIRE(alptas_decode(frame, kUtc, 480000000, 87000000, got) == Status::Ok);

    CHECK(got.addr == 0x123456u);
    CHECK(int(got.addr_table) == 0x06);
    CHECK(int(got.aircraft_cat) == 4);
    CHECK(int(got.flight_state) == 2);
    CHECK(std::abs(got.alt_m - obs.alt_m) <= 2);
    CHECK(std::abs(int(got.speed_q) - int(obs.speed_q)) <= 2);
    CHECK(std::abs(int(got.climb_e8) - int(obs.climb_e8)) <= 2);
    CHECK(std::abs(int(got.track_c9) - int(obs.track_c9)) <= 2);
    CHECK(got.has_speed);
    CHECK(got.has_climb);
    CHECK(got.valid_pos);
    CHECK(got.rx_utc == kUtc);
    CHECK(got.source == messages::Source::Alptas);
}

TEST_CASE("alptas: on-ground and airborne flight state survive the 2-bit field") {
    for (uint8_t state : {uint8_t(1), uint8_t(2)}) {
        messages::AircraftObs obs = make_obs(481234567, 87654321);
        obs.flight_state = state;
        uint8_t frame[kAlptasFrameBytes];
        REQUIRE(alptas_encode(frame, obs, kUtc, 481000000, 87000000) == Status::Ok);
        messages::AircraftObs got{};
        REQUIRE(alptas_decode(frame, kUtc, 481000000, 87000000, got) == Status::Ok);
        CHECK(int(got.flight_state) == int(state));
    }
}

TEST_CASE("alptas: an ICAO address keeps its table, an unknown one degrades to 0") {
    for (uint8_t table : {uint8_t(0x05), uint8_t(0x06), uint8_t(0x00)}) {
        messages::AircraftObs obs = make_obs(481234567, 87654321);
        obs.addr_table = table;
        obs.addr = 0xABCDEF;
        uint8_t frame[kAlptasFrameBytes];
        REQUIRE(alptas_encode(frame, obs, kUtc, 481000000, 87000000) == Status::Ok);
        messages::AircraftObs got{};
        REQUIRE(alptas_decode(frame, kUtc, 481000000, 87000000, got) == Status::Ok);
        CHECK(got.addr == 0xABCDEFu);
        CHECK(int(got.addr_table) == int(table));
    }
    // Address types the protocol has but ADS-L has no table for (random,
    // anonymous) must not be smuggled through as a real identity.
    CHECK(int(alptas_addr_type_to_table(0)) == 0);
    CHECK(int(alptas_addr_type_to_table(3)) == 0);
    CHECK(int(alptas_addr_type_to_table(1)) == 0x05);
    CHECK(int(alptas_addr_type_to_table(2)) == 0x06);
    // Bit 2 of the wire field marks a relayed frame, not a fourth address type.
    CHECK(int(alptas_addr_type_to_table(1 | 4)) == 0x05);
}

TEST_CASE("alptas: position round trips north, south, high latitude and near zero") {
    check_position_round_trip(481234567, 87654321, 480000000, 87000000);      // Black Forest
    check_position_round_trip(-339876543, 184321098, -340000000, 184000000);  // Cape Town
    check_position_round_trip(695000000, -202500000, 695500000, -202000000);  // Arctic, west
    check_position_round_trip(-15000, -25000, 10000, 10000);                  // across 0/0
    check_position_round_trip(895000000, 1795000000, 894000000, 1794000000);  // near the pole
}

TEST_CASE("alptas: position round trips across the 20-bit coding wrap") {
    // At |lat| < 14 deg the longitude quantum is 52e-7 deg, so the 20-bit field
    // wraps every 52 * 2^20 = 5.4525952 deg of longitude. A receiver one side of
    // that boundary must still place a target on the other side.
    constexpr int32_t kWrap = 52 * 1048576;  // 54525952
    check_position_round_trip(50000000, kWrap + 1900, kWrap - 9000, 50000000);
    check_position_round_trip(50000000, kWrap - 1900, kWrap + 9000, 50000000);
    // Latitude wraps every 52 * 2^20 too, at 5.4525952 deg.
    check_position_round_trip(kWrap + 1300, 50000000, kWrap - 7000, 50000000);
    check_position_round_trip(-(kWrap + 1300), 50000000, -(kWrap - 7000), 50000000);
}

TEST_CASE("alptas: speed, climb and track round trip over their ranges") {
    static const uint16_t kSpeeds[] = {0, 4, 40, 180, 400, 800};
    static const int16_t kClimbs[] = {0, 8, -8, 44, -44, 80, -80, 200};
    static const uint16_t kTracks[] = {0, 1, 128, 256, 383, 511};
    for (uint16_t speed : kSpeeds) {
        for (int16_t climb : kClimbs) {
            for (uint16_t track : kTracks) {
                messages::AircraftObs obs = make_obs(481234567, 87654321);
                obs.speed_q = speed;
                obs.climb_e8 = climb;
                obs.track_c9 = track;
                uint8_t frame[kAlptasFrameBytes];
                REQUIRE(alptas_encode(frame, obs, kUtc, 481000000, 87000000) == Status::Ok);
                messages::AircraftObs got{};
                REQUIRE(alptas_decode(frame, kUtc, 481000000, 87000000, got) == Status::Ok);
                CAPTURE(speed);
                CAPTURE(climb);
                CAPTURE(track);
                CHECK(std::abs(int(got.speed_q) - int(speed)) <= 3);
                CHECK(std::abs(int(got.climb_e8) - int(climb)) <= 3);
                int track_error = int(got.track_c9) - int(track);
                if (track_error > 256) track_error -= 512;
                if (track_error < -256) track_error += 512;
                CHECK(std::abs(track_error) <= 2);
            }
        }
    }
}

TEST_CASE("alptas: altitude round trips including below sea level and high up") {
    static const int32_t kAlts[] = {-400, -50, 0, 300, 1234, 3096, 5000, 11000};
    for (int32_t alt : kAlts) {
        messages::AircraftObs obs = make_obs(481234567, 87654321);
        obs.alt_m = alt;
        uint8_t frame[kAlptasFrameBytes];
        REQUIRE(alptas_encode(frame, obs, kUtc, 481000000, 87000000) == Status::Ok);
        messages::AircraftObs got{};
        REQUIRE(alptas_decode(frame, kUtc, 481000000, 87000000, got) == Status::Ok);
        CAPTURE(alt);
        CHECK(std::abs(got.alt_m - alt) <= 2);
    }
}

TEST_CASE("alptas: the frame CRC is set, checked, and a flipped bit fails it") {
    messages::AircraftObs obs = make_obs(481234567, 87654321);
    uint8_t frame[kAlptasFrameBytes];
    REQUIRE(alptas_encode(frame, obs, kUtc, 480000000, 87000000) == Status::Ok);
    CHECK(alptas_crc_ok(frame));

    uint16_t crc = alptas_crc(frame);
    CHECK(int(frame[kAlptasDataBytes]) == int(crc >> 8));  // high byte first on the wire
    CHECK(int(frame[kAlptasDataBytes + 1]) == int(crc & 0xFF));

    for (int bit = 0; bit < kAlptasFrameBytes * 8; bit++) {
        uint8_t bad[kAlptasFrameBytes];
        std::memcpy(bad, frame, sizeof(bad));
        bad[bit >> 3] ^= static_cast<uint8_t>(1u << (bit & 7));
        CAPTURE(bit);
        CHECK_FALSE(alptas_crc_ok(bad));
        messages::AircraftObs got{};
        CHECK(alptas_decode(bad, kUtc, 480000000, 87000000, got) == Status::Crc);
    }

    // set_crc is idempotent and repairs a clobbered checksum.
    frame[kAlptasDataBytes] ^= 0xFF;
    CHECK_FALSE(alptas_crc_ok(frame));
    alptas_set_crc(frame);
    CHECK(alptas_crc_ok(frame));
}

TEST_CASE("alptas: the decrypt gate accepts the second it was sent in, +/-1") {
    messages::AircraftObs obs = make_obs(481234567, 87654321);
    uint8_t frame[kAlptasFrameBytes];
    REQUIRE(alptas_encode(frame, obs, kUtc, 480000000, 87000000) == Status::Ok);

    messages::AircraftObs got{};
    CHECK(alptas_decode(frame, kUtc, 480000000, 87000000, got) == Status::Ok);
    CHECK(alptas_decode(frame, kUtc + 1, 480000000, 87000000, got) == Status::Ok);

    // Two seconds off is a different aircraft's frame, or ours mis-framed.
    CHECK(alptas_decode(frame, kUtc + 2, 480000000, 87000000, got) != Status::Ok);
    // No wrap fudge: 15 seconds later the low nibble is 15 against a sent 0, and
    // that is a rejection, not an off-by-one.
    CHECK(alptas_decode(frame, kUtc + 15, 480000000, 87000000, got) != Status::Ok);
    // A different 16-second block changes the key stage as well.
    CHECK(alptas_decode(frame, kUtc + 16, 480000000, 87000000, got) != Status::Ok);
    CHECK(alptas_decode(frame, kUtc - 100, 480000000, 87000000, got) != Status::Ok);
}

TEST_CASE("alptas: the decrypt gate rejects a frame with one byte changed") {
    messages::AircraftObs obs = make_obs(481234567, 87654321);
    uint8_t frame[kAlptasFrameBytes];
    REQUIRE(alptas_encode(frame, obs, kUtc, 480000000, 87000000) == Status::Ok);

    // Change one byte and repair the CRC, so the only line of defence left is the
    // plausibility of the decrypt itself.
    for (int i = 0; i < kAlptasDataBytes; i++) {
        uint8_t bad[kAlptasFrameBytes];
        std::memcpy(bad, frame, sizeof(bad));
        bad[i] = static_cast<uint8_t>(bad[i] ^ 0x5A);
        alptas_set_crc(bad);
        REQUIRE(alptas_crc_ok(bad));
        messages::AircraftObs got{};
        CAPTURE(i);
        CHECK(alptas_decode(bad, kUtc, 480000000, 87000000, got) != Status::Ok);
    }
}

TEST_CASE("alptas: the decrypt gate rejects random frames") {
    Rng rng(0xA1B2C3D4);
    int accepted = 0;
    for (int n = 0; n < 500; n++) {
        uint8_t frame[kAlptasFrameBytes];
        for (int i = 0; i < kAlptasDataBytes; i++) frame[i] = static_cast<uint8_t>(rng.next() >> 9);
        alptas_set_crc(frame);
        messages::AircraftObs got{};
        if (alptas_decode(frame, kUtc, 480000000, 87000000, got) == Status::Ok) accepted++;
    }
    CHECK(accepted == 0);
}

TEST_CASE("alptas: a non-position message type is not decoded as traffic") {
    messages::AircraftObs obs = make_obs(481234567, 87654321);
    uint8_t frame[kAlptasFrameBytes];
    REQUIRE(alptas_encode(frame, obs, kUtc, 480000000, 87000000) == Status::Ok);
    // Message type lives in the plaintext first word: 3 is a text message.
    frame[3] = static_cast<uint8_t>((frame[3] & 0xF0) | 3);
    alptas_set_crc(frame);
    messages::AircraftObs got{};
    CHECK(alptas_decode(frame, kUtc, 480000000, 87000000, got) == Status::Unsupported);
}

TEST_CASE("alptas: encoding refuses to claim a position it does not have") {
    messages::AircraftObs obs = make_obs(481234567, 87654321);
    obs.valid_pos = false;
    uint8_t frame[kAlptasFrameBytes];
    CHECK(alptas_encode(frame, obs, kUtc, 480000000, 87000000) == Status::Invalid);
}

TEST_CASE("alptas: aircraft type maps back to the ADS-L category it came from") {
    // Invertible both ways: unknown, glider, rotorcraft, skydiver, paraglider,
    // powered, balloon, UAV. The rest of the FLARM taxonomy is many-to-one into
    // ADS-L (tow plane and drop plane are both "light", gyrocopter is
    // "rotorcraft", airship is "balloon"), so only one representative survives.
    static const uint8_t kInvertible[] = {0x0, 0x1, 0x3, 0x4, 0x7, 0x8, 0xB, 0xD};
    for (uint8_t type : kInvertible) {
        CAPTURE(type);
        CHECK(int(adsl_cat_to_alptas(alptas_type_to_adsl_cat(type))) == int(type));
    }
    CHECK(int(alptas_type_to_adsl_cat(0x1)) == 4);   // glider
    CHECK(int(alptas_type_to_adsl_cat(0x8)) == 1);   // powered aircraft -> light
    CHECK(int(alptas_type_to_adsl_cat(0x9)) == 2);   // jet -> small-heavy
    CHECK(int(alptas_type_to_adsl_cat(0xD)) == 11);  // UAV
    CHECK(int(alptas_type_to_adsl_cat(0xF)) == 0);   // static object -> no info
    CHECK(int(alptas_type_to_adsl_cat(0x20)) == 0);  // out of range is not a crash

    // And the whole way round, through the wire.
    for (uint8_t cat : {uint8_t(1), uint8_t(3), uint8_t(4), uint8_t(5), uint8_t(8), uint8_t(11)}) {
        messages::AircraftObs obs = make_obs(481234567, 87654321);
        obs.aircraft_cat = cat;
        uint8_t frame[kAlptasFrameBytes];
        REQUIRE(alptas_encode(frame, obs, kUtc, 481000000, 87000000) == Status::Ok);
        messages::AircraftObs got{};
        REQUIRE(alptas_decode(frame, kUtc, 481000000, 87000000, got) == Status::Ok);
        CAPTURE(cat);
        CHECK(int(got.aircraft_cat) == int(cat));
    }
}
