// The ADS-L uplink air<->ground round-trip,
// pure host, no hardware. Proves air+ground share one core/. Asserts RS(255,223)
// corrects all patterns up to 16 symbol errors and, beyond 16, that silent
// miscorrection is counted SEPARATELY from detected failure.
#include <cstring>

#include "core/fec/reed_solomon.h"
#include "core/protocol/adsl_uplink.h"
#include "core/timing/slot.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::protocol;
using skyblip::fec::ReedSolomon255;

namespace {
// §C.5's uplink slot, which is wider at both ends than the dwell one radio can
// give it. The dwell map's own figures are in core/timing/slot.h.
constexpr int kUplinkSlotStartMs = 200;
constexpr int kUplinkSlotEndMs = 450;

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

messages::AircraftObs make_target(uint32_t addr) {
    messages::AircraftObs t{};
    t.addr = addr & 0xFFFFFF;
    t.addr_table = 6;
    t.aircraft_cat = 4;
    t.flight_state = 2;
    t.speed_q = 180;
    t.has_speed = true;
    t.lat_1e7 = 481234500;
    t.lon_1e7 = 81234500;
    t.alt_m = 1234;
    t.valid_pos = true;
    return t;
}
}  // namespace

TEST_CASE("rs: encode then decode with no errors returns 0 corrections") {
    ReedSolomon255 rs;
    uint8_t data[ReedSolomon255::kK];
    for (int i = 0; i < ReedSolomon255::kK; i++) data[i] = static_cast<uint8_t>(i * 3 + 1);
    uint8_t cw[ReedSolomon255::kN];
    std::memcpy(cw, data, ReedSolomon255::kK);
    rs.encode(data, cw + ReedSolomon255::kK);
    CHECK(rs.syndromes_zero(cw));
    CHECK(rs.decode(cw) == 0);
    CHECK(std::memcmp(cw, data, ReedSolomon255::kK) == 0);
}

TEST_CASE("rs: corrects ALL patterns up to 16 symbol errors") {
    ReedSolomon255 rs;
    Rng rng(0xBEEF);
    uint8_t data[ReedSolomon255::kK];
    for (int i = 0; i < ReedSolomon255::kK; i++) data[i] = static_cast<uint8_t>(rng.next());
    uint8_t base[ReedSolomon255::kN];
    std::memcpy(base, data, ReedSolomon255::kK);
    rs.encode(data, base + ReedSolomon255::kK);

    for (int nerr = 1; nerr <= 16; nerr++) {
        for (int trial = 0; trial < 200; trial++) {
            uint8_t cw[ReedSolomon255::kN];
            std::memcpy(cw, base, ReedSolomon255::kN);
            bool used[ReedSolomon255::kN] = {false};
            for (int e = 0; e < nerr; e++) {
                int pos;
                do {
                    pos = rng.next() % ReedSolomon255::kN;
                } while (used[pos]);
                used[pos] = true;
                uint8_t v;
                do {
                    v = static_cast<uint8_t>(rng.next());
                } while (v == 0);
                cw[pos] ^= v;
            }
            int corrected = rs.decode(cw);
            CHECK(corrected == nerr);
            CHECK(std::memcmp(cw, base, ReedSolomon255::kN) == 0);
        }
    }
}

TEST_CASE("rs: beyond 16 errors, detected vs silent miscorrection, counted") {
    ReedSolomon255 rs;
    Rng rng(0x1234);
    uint8_t data[ReedSolomon255::kK];
    for (int i = 0; i < ReedSolomon255::kK; i++) data[i] = static_cast<uint8_t>(rng.next());
    uint8_t base[ReedSolomon255::kN];
    std::memcpy(base, data, ReedSolomon255::kK);
    rs.encode(data, base + ReedSolomon255::kK);

    int detected = 0, silent = 0, lucky = 0;
    const int kTrials = 4000;
    for (int t = 0; t < kTrials; t++) {
        uint8_t cw[ReedSolomon255::kN];
        std::memcpy(cw, base, ReedSolomon255::kN);
        int nerr = 17 + (rng.next() % 20);  // 17..36 errors
        bool used[ReedSolomon255::kN] = {false};
        for (int e = 0; e < nerr; e++) {
            int pos;
            do {
                pos = rng.next() % ReedSolomon255::kN;
            } while (used[pos]);
            used[pos] = true;
            uint8_t v;
            do {
                v = static_cast<uint8_t>(rng.next());
            } while (v == 0);
            cw[pos] ^= v;
        }
        int r = rs.decode(cw);
        if (r < 0) {
            detected++;  // safe
        } else if (std::memcmp(cw, base, ReedSolomon255::kN) == 0) {
            lucky++;  // happened to fully recover (rare)
        } else {
            silent++;  // decoded to a WRONG codeword: the danger mode
        }
    }
    MESSAGE("detected=" << detected << " silent=" << silent << " lucky=" << lucky);
    // The vast majority must be safely detected. Silent miscorrection is bounded
    // by the RS structure and must stay a small minority (and is measured, not
    // hidden). decode() re-verifies syndromes so silent should be ~0 here.
    CHECK(detected > kTrials * 3 / 4);
    CHECK(silent <= 5);
}

TEST_CASE("uplink: air<->ground round-trip is lossless at BER 0 (anti-drift lock)") {
    AdslUplink up;
    messages::AircraftObs tx[AdslUplink::kMaxTargets];
    int n = AdslUplink::kMaxTargets;
    for (int i = 0; i < n; i++) tx[i] = make_target(0x200000 + i);

    uint8_t frame[AdslUplink::kFrameBytes];
    CHECK(up.encode(tx, n, /*key=*/0, frame) == Status::Ok);

    messages::AircraftObs rx[AdslUplink::kMaxTargets];
    AdslUplink::DecodeStats st;
    CHECK(up.decode(frame, rx, n, st) == Status::Ok);
    CHECK(st.targets == n);
    CHECK(st.corrected == 0);
    for (int i = 0; i < n; i++) {
        CHECK(rx[i].addr == tx[i].addr);
        CHECK(rx[i].lat_1e7 == tx[i].lat_1e7);
        CHECK(rx[i].lon_1e7 == tx[i].lon_1e7);
        CHECK(rx[i].alt_m == tx[i].alt_m);
        CHECK(int(rx[i].aircraft_cat) == int(tx[i].aircraft_cat));
    }
}

TEST_CASE("uplink: survives up to 16 symbol errors on the wire") {
    AdslUplink up;
    Rng rng(0x55AA);
    messages::AircraftObs tx[4];
    for (int i = 0; i < 4; i++) tx[i] = make_target(0x300000 + i * 7);
    uint8_t frame[AdslUplink::kFrameBytes];
    REQUIRE(up.encode(tx, 4, 0, frame) == Status::Ok);

    // inject 16 symbol errors
    bool used[AdslUplink::kFrameBytes] = {false};
    for (int e = 0; e < 16; e++) {
        int pos;
        do {
            pos = rng.next() % AdslUplink::kFrameBytes;
        } while (used[pos]);
        used[pos] = true;
        frame[pos] ^= static_cast<uint8_t>(0x80 | (rng.next() & 0x7F));
    }
    messages::AircraftObs rx[4];
    AdslUplink::DecodeStats st;
    CHECK(up.decode(frame, rx, 4, st) == Status::Ok);
    CHECK(st.corrected == 16);
    CHECK(st.targets == 4);
    CHECK(rx[0].addr == tx[0].addr);
    CHECK(rx[3].lat_1e7 == tx[3].lat_1e7);
}

TEST_CASE("uplink: a destroyed frame is DETECTED (Status::Crc), never silently wrong") {
    AdslUplink up;
    messages::AircraftObs tx[2] = {make_target(1), make_target(2)};
    uint8_t frame[AdslUplink::kFrameBytes];
    REQUIRE(up.encode(tx, 2, 0, frame) == Status::Ok);
    for (int i = 0; i < 40; i++) frame[i] ^= 0xFF;  // 40 > 16 errors
    messages::AircraftObs rx[2];
    AdslUplink::DecodeStats st;
    Status s = up.decode(frame, rx, 2, st);
    CHECK(s == Status::Crc);  // detected, not delivered
}

// What the dwell has to be armed with to hear a skyPost at all. Every one of
// these is a number the receiver and the transmitter must agree on, and the
// third was wrong: 0x18 is the M band's ADS-L data length, 24 bytes, and it was
// carried over to a frame that is 255. A detector armed for it never matched.
TEST_CASE("uplink: the sync word is C.4.3's, and the byte behind it is D.1.1's length") {
    CHECK(kUplinkSync[0] == 0x2D);
    CHECK(kUplinkSync[1] == 0xD4);
    CHECK(kUplinkSync[2] == kUplinkFrameBytes);
    CHECK(kUplinkFrameBytes == AdslUplink::kFrameBytes);
    CHECK(kUplinkSyncBits == 24);
    CHECK(kUplinkBurstBytes == 3 + AdslUplink::kFrameBytes);
    // §C.4: 200 kbps GMSK, BT 0.5, in a 250 kHz channel, ±50 kHz deviation.
    CHECK(kUplinkChipRateBps == 200000);
    CHECK(kUplinkDeviationHz == 50000);
    CHECK(kUplinkChannelBandwidthHz == 250000);
    CHECK(kUplinkGaussianBtE2 == 50);
}

// Where the burst sits in the second, which is the fourth thing a dwell and a
// transmitter have to agree on after the sync word, the length and the rate.
//
// A full frame is 10.44 ms of air at §C.4's 200 kbps, and core/timing/slot.h
// cuts the O-band dwell at 205..395 because our own skyPost is on air from 210
// and "completes by 390". These are the sums behind that sentence.
TEST_CASE("uplink: a frame fits the window the dwell map is cut against") {
    CHECK(kUplinkBurstUs == 10440);

    // Our own ground station: on air at 210, done at 220.44, and the dwell that
    // was listening for it opened at 205 and closes at 395.
    const uint32_t ours_start_us = timing::kGroundEmitStart * 1000u;
    CHECK(ours_start_us > timing::kUplinkRxStart * 1000u);
    CHECK(ours_start_us + kUplinkBurstUs < timing::kUplinkRxEnd * 1000u);

    // kGroundEmitEnd is a completion deadline and not a start deadline, and the
    // difference is 10.44 ms: a burst BEGUN at 390 would still be on air at 400,
    // where the radio has already retuned to the M band.
    CHECK(timing::kGroundEmitEnd * 1000u + kUplinkBurstUs > timing::kUplinkRxEnd * 1000u);
    const uint32_t last_start_us = timing::kGroundEmitEnd * 1000u - kUplinkBurstUs;
    CHECK(last_start_us > timing::kUplinkRxStart * 1000u);

    // And the limit this dwell has, stated rather than discovered: §C.5 gives
    // the uplink slot 200..450 ms and only requires a transmission to complete
    // before it ends, so a ground station that is not ours may legally begin one
    // as late as 439.56 ms. One radio cannot be on two bands at once and the
    // M-band dwell opens at 400 (§C.2.5 costs half the neighbours if we skip a
    // channel), so a third-party uplink transmitted in the second half of its
    // slot is one we do not hear. skyPost transmits at 210 for exactly this
    // reason, and hearing our own network is the capability we ship; a
    // third-party station that starts late is a known and accepted deaf spot.
    const uint32_t conforming_last_start_us = kUplinkSlotEndMs * 1000u - kUplinkBurstUs;
    CHECK(conforming_last_start_us > timing::kUplinkRxEnd * 1000u);
    CHECK(kUplinkSlotStartMs < timing::kUplinkRxStart);
}

// Reed-Solomon can decode to a valid codeword that is not the one that was sent.
// A ground station reaches every aircraft in range at once, so one miscorrected
// frame is a screenful of phantoms in front of everybody, and what a frame
// carries is checked before any of it becomes a target.
TEST_CASE("uplink: a record with no address and no place on the globe is refused") {
    AdslUplink up;
    messages::AircraftObs tx[3] = {make_target(0x600001), make_target(0), make_target(0x600003)};
    tx[2].lat_1e7 = 1200000000;  // 120 degrees north of nowhere
    uint8_t frame[AdslUplink::kFrameBytes];
    REQUIRE(up.encode(tx, 3, 0, frame) == Status::Ok);

    messages::AircraftObs rx[3];
    AdslUplink::DecodeStats st;
    CHECK(up.decode(frame, rx, 3, st) == Status::Ok);
    CHECK(st.targets == 1);
    CHECK(st.rejected == 2);
    CHECK(rx[0].addr == 0x600001u);
}
