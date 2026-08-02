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
    own.alt_m = 1500;  // HAE: what G.1.7 transmits
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

// B4. Stealth is a real setting in this domain and ADS-L has no bit for it:
// SoftRF forces the transmitted vertical speed to zero and sets the FLARM
// stealth bit (oss/SoftRF-lyusupov .../src/protocol/radio/Legacy.cpp:300,
// 308-309). Zero is a lie - it claims level flight - so ours says "unavailable"
// with the code G.1.9 provides, and drops the address table to an anonymous one.
TEST_CASE("adsl: stealth withholds the climb rate and claims no registered identity") {
    skyblip::messages::OwnState own{};
    own.fix_valid = true;
    own.climb_valid = true;
    own.lat_1e7 = 485000000;
    own.lon_1e7 = 85000000;
    own.alt_m = 1500;
    own.speed_q = 200;
    own.climb_e8 = 24;
    own.hdop_e2 = 90;

    AdslPacket open{};
    from_own(open, own, 0x3C0A11, 5, 4, /*stealth=*/false);
    CHECK(open.has_climb());
    CHECK(open.climb_e8() == 24);
    CHECK(int(open.addr_table()) == 5);
    CHECK(open.address() == 0x3C0A11u);

    AdslPacket hidden{};
    from_own(hidden, own, 0x3C0A11, 5, 4, /*stealth=*/true);
    CHECK_FALSE(hidden.has_climb());
    CHECK(int(hidden.addr_table()) == 0);

    // Everything a collision alarm needs is still transmitted: stealth hides the
    // climb and the registry, never the aircraft.
    CHECK(hidden.alt_m() == 1500);
    CHECK(hidden.has_speed());
    CHECK(hidden.lat_1e7() == open.lat_1e7());
    CHECK(int(hidden.HorizAccuracy) == int(open.HorizAccuracy));
}

// F4. The address goes on the air here, and the shell hands us the chip id
// straight from hwinfo, so this is the last place that can move it.
TEST_CASE("adsl: a self-minted address is moved off a crowded prefix on its way out") {
    skyblip::messages::OwnState own{};
    own.fix_valid = true;

    AdslPacket anonymous{};
    from_own(anonymous, own, 0xDD1234, 0, 4, false);
    CHECK(anonymous.address() == 0xED1234u);

    // An address that was issued to the aircraft is transmitted as issued,
    // whatever prefix it carries: it is not ours to move.
    AdslPacket icao{};
    from_own(icao, own, 0xDD1234, 5, 4, false);
    CHECK(icao.address() == 0xDD1234u);

    AdslPacket flarm{};
    from_own(flarm, own, 0x111111, 6, 4, false);
    CHECK(flarm.address() == 0x111111u);
}

// Every field of the Integrity block has a zero code meaning "unknown / no fix"
// (G.1.10 to G.1.15), so an untouched block is not neutral: it tells a receiver
// we claim no integrity at all, and receivers are entitled to weight us
// accordingly. Without a fix that is the truthful answer; with one it is not.
TEST_CASE("adsl: from_own claims integrity from the receiver's DOP") {
    skyblip::messages::OwnState own{};
    own.lat_1e7 = 485000000;
    own.lon_1e7 = 85000000;
    own.alt_m = 1500;
    own.speed_q = 200;
    own.hdop_e2 = 90;

    AdslPacket p{};
    from_own(p, own, 0xABCDEF, 6, 4, false);
    CHECK(int(p.SourceIntegrity) == 0);
    CHECK(int(p.NavigIntegrity) == 0);
    CHECK(int(p.HorizAccuracy) == 0);
    CHECK(int(p.VertAccuracy) == 0);
    CHECK(int(p.VelAccuracy) == 0);

    // HDOP 0.9 with a fix: 1.8 m horizontal, 2.7 m vertical.
    own.fix_valid = true;
    from_own(p, own, 0xABCDEF, 6, 4, false);
    CHECK(int(p.SourceIntegrity) == AdslPacket::kSourceIntegrity1e3);
    CHECK(int(p.DesignAssurance) == AdslPacket::kDesignAssuranceNone);
    CHECK(int(p.NavigIntegrity) == 12);  // Rc < 7.5 m
    CHECK(int(p.HorizAccuracy) == 7);    // HFOM < 3 m
    CHECK(int(p.VertAccuracy) == 3);     // VFOM < 10 m
    CHECK(int(p.VelAccuracy) == 3);      // < 1 m/s

    // A receiver that reports no HDOP is not a receiver reporting a good one.
    own.hdop_e2 = 0;
    from_own(p, own, 0xABCDEF, 6, 4, false);
    CHECK(int(p.SourceIntegrity) == 0);
    CHECK(int(p.HorizAccuracy) == 0);
}

// G.1.13 NACp boundaries, in metres of horizontal figure of merit: 7 is under
// 3 m, 6 under 10 m, 5 under 30 m, 4 under 0.05 NM (92.6 m), and 0 is 0.5 NM or
// worse, which is the same code as "no fix".
TEST_CASE("adsl: horizontal accuracy code sits on the G.1.13 boundaries") {
    CHECK(int(AdslPacket::horizontal_accuracy_code(299)) == 7);
    CHECK(int(AdslPacket::horizontal_accuracy_code(300)) == 6);
    CHECK(int(AdslPacket::horizontal_accuracy_code(999)) == 6);
    CHECK(int(AdslPacket::horizontal_accuracy_code(1000)) == 5);
    CHECK(int(AdslPacket::horizontal_accuracy_code(2999)) == 5);
    CHECK(int(AdslPacket::horizontal_accuracy_code(3000)) == 4);
    CHECK(int(AdslPacket::horizontal_accuracy_code(9259)) == 4);
    CHECK(int(AdslPacket::horizontal_accuracy_code(9260)) == 3);
    CHECK(int(AdslPacket::horizontal_accuracy_code(92600)) == 0);
}

// G.1.14 GVA: 3 under 10 m, 2 under 45 m, 1 under 150 m, 0 beyond.
TEST_CASE("adsl: vertical accuracy code sits on the G.1.14 boundaries") {
    CHECK(int(AdslPacket::vertical_accuracy_code(999)) == 3);
    CHECK(int(AdslPacket::vertical_accuracy_code(1000)) == 2);
    CHECK(int(AdslPacket::vertical_accuracy_code(4499)) == 2);
    CHECK(int(AdslPacket::vertical_accuracy_code(4500)) == 1);
    CHECK(int(AdslPacket::vertical_accuracy_code(14999)) == 1);
    CHECK(int(AdslPacket::vertical_accuracy_code(15000)) == 0);
}

// G.1.12 NIC: the containment radius. Without RAIM or a protection level the
// radius we claim is the DOP-derived accuracy itself, and SourceIntegrity is
// what says how much that claim is worth.
TEST_CASE("adsl: navigation integrity code sits on the G.1.12 boundaries") {
    CHECK(int(AdslPacket::navigation_integrity_code(749)) == 12);
    CHECK(int(AdslPacket::navigation_integrity_code(750)) == 11);
    CHECK(int(AdslPacket::navigation_integrity_code(2499)) == 11);
    CHECK(int(AdslPacket::navigation_integrity_code(2500)) == 10);
    CHECK(int(AdslPacket::navigation_integrity_code(7499)) == 10);
    CHECK(int(AdslPacket::navigation_integrity_code(7500)) == 9);
    CHECK(int(AdslPacket::navigation_integrity_code(3703000)) == 1);
}

// A degrading fix walks the codes down together, and a hopeless one claims
// nothing rather than claiming a number nobody should act on.
TEST_CASE("adsl: a degrading HDOP walks the accuracy claim down") {
    skyblip::messages::OwnState own{};
    own.fix_valid = true;
    AdslPacket p{};

    own.hdop_e2 = 200;  // 4.0 m horizontal, 6.0 m vertical
    from_own(p, own, 1, 6, 4, false);
    CHECK(int(p.HorizAccuracy) == 6);
    CHECK(int(p.VertAccuracy) == 3);
    CHECK(int(p.VelAccuracy) == 2);

    own.hdop_e2 = 800;  // 16 m horizontal, 24 m vertical
    from_own(p, own, 1, 6, 4, false);
    CHECK(int(p.HorizAccuracy) == 5);
    CHECK(int(p.VertAccuracy) == 2);
    CHECK(int(p.VelAccuracy) == 1);
    CHECK(int(p.NavigIntegrity) == 11);  // Rc 7.5 to 25 m

    own.hdop_e2 = 9999;  // 200 m horizontal, 300 m vertical
    from_own(p, own, 1, 6, 4, false);
    CHECK(int(p.HorizAccuracy) == 2);  // 0.1 to 0.3 NM
    CHECK(int(p.VertAccuracy) == 0);   // beyond 150 m: no claim at all
    CHECK(int(p.VelAccuracy) == 0);

    own.hdop_e2 = 60000;  // 1200 m: beyond 0.5 NM, the same code as no fix
    from_own(p, own, 1, 6, 4, false);
    CHECK(int(p.HorizAccuracy) == 0);
    CHECK(int(p.NavigIntegrity) == 6);  // Rc 0.6 to 1 NM, still a bounded claim
}
