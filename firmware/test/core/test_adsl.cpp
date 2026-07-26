// ADS-L position packet: the safety-critical round-trip and error-correction
// tests (roadmap 2.1, 3-ARCHITECTURE §3 "test a radio protocol with zero RF").
// The RF channel is reduced to an injectable BER; we distinguish *detected
// failure* from *silent miscorrection* (the real anti-collision danger).
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
    // On air these 21 bytes ride; the RX recomputes.
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

TEST_CASE("adsl: Monte-Carlo BER — detected vs silent miscorrection accounting") {
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

        // Inject up to 3 bit errors; flag ~70% of them as weak.
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
    // codeword; with only <=3 injected bits it must be essentially zero here.
    CHECK(silent <= 2);
}
