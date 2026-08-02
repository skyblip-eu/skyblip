// The ADS-L position packet end to end, a radio protocol checked with no RF
// hardware in the room: the channel is an injectable bit error rate, and the
// packet has to survive it or say that it did not. What these separate is a
// *detected failure*, a frame dropped and counted, from a *silent
// miscorrection*, a frame that decodes to a plausible aircraft somewhere it is
// not. Only the second one collides with anybody.
#include <cstdlib>  // std::abs - libc++ pulls it in transitively, libstdc++ does not
#include <cstring>

#include "core/protocol/adsl.h"
#include "doctest/doctest.h"

using namespace skyblip::protocol;

namespace {
// Deterministic xorshift so Monte-Carlo runs are reproducible in CI.
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

AdslPacket make_reference() {
    AdslPacket p;
    p.init();
    p.set_address(0x123456);
    p.set_addr_table(6);  // FLARM/ALP-TAS table
    p.TimeStamp = 4 * 4;  // 4.0 s
    p.FlightState = 2;    // airborne
    p.AcftCat = 4;        // glider
    p.Emergency = 1;
    p.set_lat_1e7(481234500);  // 48.12345 deg
    p.set_lon_1e7(81234500);   //  8.12345 deg
    p.set_alt_m(1234);
    p.set_speed_q(45 * 4);
    p.set_climb_e8(-44);  // -5.5 m/s
    p.set_track_c9(256);  // 180 deg
    p.SourceIntegrity = 3;
    p.DesignAssurance = 2;
    p.NavigIntegrity = 11;
    p.HorizAccuracy = 6;
    p.VertAccuracy = 3;
    p.VelAccuracy = 2;
    return p;
}
}  // namespace

TEST_CASE("adsl: field accessors round-trip through the wire layout") {
    AdslPacket p = make_reference();
    CHECK(p.address() == 0x123456);
    CHECK(p.addr_table() == 6);
    CHECK(int(p.TimeStamp) == 16);
    CHECK(int(p.FlightState) == 2);
    CHECK(int(p.AcftCat) == 4);
    CHECK(std::abs(p.lat_1e7() - 481234500) < 200);
    CHECK(std::abs(p.lon_1e7() - 81234500) < 200);
    CHECK(std::abs(p.alt_m() - 1234) <= 8);
    CHECK(std::abs(int(p.speed_q()) - 180) <= 4);
    CHECK(p.has_climb());
    CHECK(std::abs(int(p.climb_e8()) - (-44)) <= 8);
    CHECK(p.track_c9() == 256);
}

TEST_CASE("adsl: scramble+crc, then descramble+check round-trip is lossless (BER 0)") {
    AdslPacket p = make_reference();
    AdslPacket golden = p;
    p.scramble();
    p.set_crc();
    // On air these 21 bytes ride. The RX recomputes.
    CHECK(p.check_crc() == 0);
    p.descramble();
    // payload identical to golden (compare the scrambled region bytes)
    CHECK(std::memcmp(p.Byte, golden.Byte, 20) == 0);
    CHECK(p.address() == golden.address());
    CHECK(p.lat_1e7() == golden.lat_1e7());
}

TEST_CASE("adsl: single-bit error is corrected via CRC syndrome") {
    AdslPacket p = make_reference();
    p.scramble();
    p.set_crc();
    uint8_t err[AdslPacket::kDataBytes] = {0};
    // flip a data bit (in the CRC-covered region)
    reinterpret_cast<uint8_t*>(&p.Version)[7] ^= 0x08;
    CHECK(p.check_crc() != 0);
    int corrected = p.correct(err, 6);
    CHECK(corrected == 1);
    CHECK(p.check_crc() == 0);
}

TEST_CASE("adsl: multi-bit errors within flagged (weak) bits are corrected") {
    AdslPacket p = make_reference();
    p.scramble();
    p.set_crc();
    uint8_t* d = reinterpret_cast<uint8_t*>(&p.Version);
    uint8_t err[AdslPacket::kDataBytes] = {0};
    // flip 3 bits and mark them weak (as a Manchester decoder would)
    struct BitLoc {
        int byte;
        uint8_t mask;
    } locs[3] = {{3, 0x40}, {9, 0x02}, {15, 0x80}};
    for (auto& l : locs) {
        d[l.byte] ^= l.mask;
        err[l.byte] |= l.mask;
    }
    CHECK(p.check_crc() != 0);
    int corrected = p.correct(err, 6);
    CHECK(corrected >= 0);
    CHECK(p.check_crc() == 0);
}

TEST_CASE("adsl: Monte-Carlo BER, detected vs silent miscorrection accounting") {
    // Push random-ish bit errors, some flagged (weak) and some not, and count:
    //   good      = decoded to the exact original codeword
    //   detected  = check_crc != 0 after correct() (safe: we know it failed)
    //   silent    = check_crc == 0 but payload != original (the DANGER mode)
    // We assert silent miscorrection stays rare and is *counted*, never hidden.
    Rng rng(0xC0FFEE);
    const int kTrials = 20000;
    int good = 0, detected = 0, silent = 0;

    for (int t = 0; t < kTrials; t++) {
        AdslPacket tx = make_reference();
        tx.set_address(0x100000 + (rng.next() & 0xFFFFF));
        tx.scramble();
        tx.set_crc();

        AdslPacket rx = tx;
        uint8_t* d = reinterpret_cast<uint8_t*>(&rx.Version);
        uint8_t err[AdslPacket::kDataBytes] = {0};

        // Inject up to 3 bit errors, and flag ~70% of them as weak.
        int nerr = (rng.next() % 4);  // 0..3
        for (int e = 0; e < nerr; e++) {
            int bit = rng.next() % (AdslPacket::kDataBytes * 8);
            int byte = bit >> 3;
            uint8_t mask = static_cast<uint8_t>(1 << (7 - (bit & 7)));
            d[byte] ^= mask;
            if ((rng.next() % 10) < 7) err[byte] |= mask;
        }

        rx.correct(err, 6);
        bool crc_ok = (rx.check_crc() == 0);
        bool same = (std::memcmp(rx.Byte, tx.Byte, 20) == 0) && rx.Version == tx.Version;

        if (crc_ok && same)
            good++;
        else if (!crc_ok)
            detected++;
        else
            silent++;  // crc_ok but wrong -> silent miscorrection
    }

    MESSAGE("good=" << good << " detected=" << detected << " silent=" << silent);
    CHECK(good > 0);
    // A 24-bit CRC bounds the silent-miscorrection probability ~2^-24 per random
    // codeword. With only <=3 injected bits it must be essentially zero here.
    CHECK(silent <= 2);
}

// --- ADS-L 4 SRD860 issue 2, G.1.7 / G.1.8 / G.1.9 -------------------------
// The three variable-range fields each have an "invalid" code, and uns_vr_encode
// saturates at exactly that code. The spec is explicit that an out-of-range
// measurement encodes the LIMIT and that "invalid" means unavailable, so
// saturation must stop one short. These pin that apart.

TEST_CASE("adsl: an over-range altitude encodes the limit, not 'unavailable'") {
    AdslPacket p{};
    p.init();
    // G.1.7 worked example: 61104 m or more -> 0x3ffe, invalid -> 0x3fff.
    p.set_alt_m(61104);
    CHECK_FALSE(p.alt_invalid());
    p.set_alt_m(100000);  // far past the maximum
    CHECK_FALSE(p.alt_invalid());
    p.set_alt_m(2000000000);
    CHECK_FALSE(p.alt_invalid());
    // And the spec's other examples still round-trip.
    p.set_alt_m(0);
    CHECK(p.alt_m() == 0);
    p.set_alt_m(1000);
    CHECK(p.alt_m() == 1000);
    p.set_alt_m(-320);
    CHECK(p.alt_m() == -320);
    p.set_alt_m(-5000);  // below the minimum clamps to it, not to invalid
    CHECK(p.alt_m() == -320);
    CHECK_FALSE(p.alt_invalid());
}

TEST_CASE("adsl: an over-range ground speed encodes the limit, not 'unavailable'") {
    AdslPacket p{};
    p.init();
    p.set_speed_q(236 * 4);  // G.1.8 maximum, 236 m/s
    CHECK(p.has_speed());
    p.set_speed_q(65535);
    CHECK(p.has_speed());
}

TEST_CASE("adsl: an over-range sink rate encodes the limit, not 'unavailable'") {
    AdslPacket p{};
    p.init();
    p.set_climb_e8(952);  // G.1.9 maximum, +119 m/s
    CHECK(p.has_climb());
    p.set_climb_e8(-944);  // G.1.9 minimum, -118 m/s
    CHECK(p.has_climb());
    p.set_climb_e8(-32768);  // absurd sink: still a rate, not "unknown"
    CHECK(p.has_climb());
    p.set_climb_e8(32767);
    CHECK(p.has_climb());
}

TEST_CASE("adsl: the invalid codes are reachable on purpose and round-trip") {
    AdslPacket p{};
    p.init();
    p.set_alt_m(1000);
    p.set_speed_q(100);
    p.set_climb_e8(8);
    REQUIRE_FALSE(p.alt_invalid());
    REQUIRE(p.has_speed());
    REQUIRE(p.has_climb());

    p.set_alt_invalid();
    p.set_speed_invalid();
    p.set_climb_invalid();
    CHECK(p.alt_invalid());
    CHECK_FALSE(p.has_speed());
    CHECK_FALSE(p.has_climb());

    // Marking one field invalid must not corrupt its packed neighbours: altitude,
    // climb and track share bytes 7..10.
    p.set_alt_m(1000);
    p.set_climb_invalid();
    p.set_track_c9(256);
    CHECK(p.alt_m() == 1000);
    CHECK_FALSE(p.has_climb());
    CHECK(p.track_c9() == 256);
    p.set_climb_e8(-16);
    CHECK(p.alt_m() == 1000);
    CHECK(p.climb_e8() == -16);
    CHECK(p.track_c9() == 256);
}

TEST_CASE("adsl: from_own marks what own-ship does not know") {
    skyblip::messages::OwnState own{};
    own.lat_1e7 = 485000000;
    own.lon_1e7 = 85000000;
    own.alt_m = 1500;
    own.speed_q = 200;
    own.climb_e8 = 16;

    // No fix yet: altitude and ground speed are unavailable, not zero.
    AdslPacket p{};
    from_own(p, own, 0xABCDEF, 6, 4, false);
    CHECK(p.alt_invalid());
    CHECK_FALSE(p.has_speed());
    CHECK_FALSE(p.has_climb());  // climb_valid is false too

    // Fix, but no vertical rate derived yet: only the climb stays unavailable.
    own.fix_valid = true;
    from_own(p, own, 0xABCDEF, 6, 4, false);
    CHECK_FALSE(p.alt_invalid());
    CHECK(p.alt_m() == 1500);
    CHECK(p.has_speed());
    CHECK_FALSE(p.has_climb());

    // Everything known.
    own.climb_valid = true;
    from_own(p, own, 0xABCDEF, 6, 4, false);
    CHECK(p.has_climb());
    CHECK(p.climb_e8() == 16);
}
