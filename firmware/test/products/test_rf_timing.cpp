// What is on the 868 MHz air, and when. Every burst in these tests is a real
// scrambled ADS-L frame put on the channel at an absolute instant. The radio
// hears it only if the firmware had the receiver tuned there at that moment.
// A slot-map regression shows up as DEAF records, not as a missing feature.
#include <cstring>
#include <string>

#include "core/gnss/first_fix.h"
#include "core/timing/slot.h"
#include "core/timing/timing_stats.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "simulator/simulator.h"

using namespace skyblip;

namespace {

// A receiver-only world: PPS unlocked keeps own-ship off air (ADS-L D.3 fails
// closed without an anchored clock), so the only bursts are the neighbour's.
simulator::Simulator listening_at(int phase_ms, int slot) {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_pps_locked(false);
    h.world().add_aircraft(800, 0, 0, 30, 180, phase_ms, slot);
    return h;
}

// F5: own-ship holds every burst until the receiver's first solutions have
// settled, so a transmit test that starts the clock at zero is a test of the
// settling window and nothing else. This is the far side of it, with the tape
// wiped so the bursts that follow are the ones under test.
uint32_t past_settling(simulator::Simulator& h) {
    h.run(gnss::kFirstFixSettleMs);
    h.world().air().clear();
    return gnss::kFirstFixSettleMs;
}

void run_on(simulator::Simulator& h, uint32_t from_ms, uint32_t for_ms) {
    for (uint32_t t = from_ms; t <= from_ms + for_ms; t += simulator::Simulator::kStepMs) h.step(t);
}

int count_of(const simulator::Air& air, simulator::AirEvent want) {
    int n = 0;
    for (int i = 0; i < air.record_count(); i++)
        if (air.record(i).event == want) n++;
    return n;
}

}  // namespace

TEST_CASE("rf: a burst is heard only inside the dwell that owns its channel") {
    struct Case {
        int phase_ms;
        int slot;
        bool heard;
    };
    // The O-band dwell runs 205..395 (framed on our ground station's burst), the
    // M-band dwells 400..800 on 868.2 and 800..1000 on 868.4. 400..450 is the
    // slice we listen to but do not transmit in: FLARM-generation traffic lives
    // there.
    const Case cases[] = {
        {50, 0, false},
        {150, 0, false},
        {300, 0, false},
        {390, 0, false},
        {420, 0, true},
        {470, 0, true},
        {600, 0, true},
        {780, 0, true},
        {820, 1, true},
        {900, 1, true},
        {990, 1, true},
        // The second dwell reaches 1200 ms: its tail is heard on 868.4, not
        // deaf. Nothing of ours transmits there, everyone else still may.
        {100, 1, true},
        {190, 1, true},
        // Right time, wrong channel: a slot-1 burst still on 868.2 is a burst
        // we cannot hear, which is why the channel is part of the dwell.
        {900, 0, false},
        // Right channel, wrong time: 868.4 during slot 0.
        {600, 1, false},
    };
    for (const Case& c : cases) {
        simulator::Simulator h = listening_at(c.phase_ms, c.slot);
        h.run(4000);
        const int heard = count_of(h.world().air(), simulator::AirEvent::Rx);
        const int deaf = count_of(h.world().air(), simulator::AirEvent::Deaf);
        CAPTURE(c.phase_ms);
        CAPTURE(c.slot);
        CHECK((heard > 0) == c.heard);
        CHECK((deaf > 0) == !c.heard);
        CHECK((h.product().state().rx_ok > 0u) == c.heard);
    }
}

// A burst near the end of the second finishes after the second has rolled over.
// Its phase is its own instant's, not the instant we noticed it ended.
TEST_CASE("rf: a burst that straddles the second is dated by when it started") {
    simulator::Simulator h = listening_at(995, 1);
    h.run(4000);
    const simulator::Air& air = h.world().air();
    REQUIRE(air.record_count() > 0);
    for (int i = 0; i < air.record_count(); i++) CHECK(air.record(i).phase_ms == 995);
}

TEST_CASE("rf: a heard burst is the frame that was on air, decoded by the real path") {
    simulator::Simulator h = listening_at(600, 0);
    h.run(3000);
    REQUIRE(h.product().state().rx_ok > 0);
    REQUIRE(h.product().state().traffic.count() == 1);
    char line[160];
    REQUIRE(h.world().air().format(0, line, sizeof(line)) > 0);
    // The tape decodes what the receiver decoded: same address, CRC intact.
    CHECK(std::string(line).find("300001") != std::string::npos);
    CHECK(std::string(line).find("crc ok") != std::string::npos);
    CHECK(std::string(line).find("868.200") != std::string::npos);
}

TEST_CASE("rf: own-ship transmits once a second, inside its window, alternating channel") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);
    const uint32_t from_ms = past_settling(h);
    run_on(h, from_ms, 6000);

    const simulator::Air& air = h.world().air();
    int transmissions = 0;
    uint32_t last_freq = 0;
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& r = air.record(i);
        if (r.event != simulator::AirEvent::Tx) continue;
        transmissions++;
        // Always inside the direct slot, even though the dwell that carries the
        // burst runs 200 ms past it.
        CHECK(timing::Scheduler::in_direct_slot(r.phase_ms));
        CHECK(r.phase_ms + timing::Transmitter::kAirTimeMs <= timing::kDirectEnd);
        if (last_freq != 0) CHECK(r.freq_hz != last_freq);
        last_freq = r.freq_hz;
    }
    CHECK(transmissions >= 4);
    CHECK(h.product().state().tx_ok == static_cast<uint32_t>(transmissions));

    // E1 and E2, read off the service that spent them: the floor the carrier
    // sense threshold is derived from, and every millisecond that went on air.
    CHECK(h.product().radio().noise_floor().samples() > 0);
    CHECK(h.product().radio().noise_floor().dbm() < timing::NoiseFloor::kSeedDbm);
    CHECK(h.product().radio().transmitter().air_time().total_ms() ==
          static_cast<uint32_t>(transmissions) * timing::Transmitter::kAirTimeMs);
    CHECK(h.product().radio().duty_permille(from_ms + 6000) < timing::AirTime::kLimitPermille);
    CHECK_FALSE(h.product().radio().over_budget());
}

TEST_CASE("rf: what own-ship put on air decodes back to own-ship state") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);
    h.world().set_altitude_m(1200);
    run_on(h, past_settling(h), 3000);

    const simulator::Air& air = h.world().air();
    int checked = 0;
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& r = air.record(i);
        if (r.event != simulator::AirEvent::Tx) continue;
        // Own bursts go on air as chips behind the ADS-L sync word, so reading
        // them back means framing them the way the receiver does.
        protocol::Frame frame{};
        REQUIRE(simulator::Air::framed(r, frame));
        REQUIRE(frame.system == protocol::System::AdslDirect);
        protocol::AdslPacket p{};
        p.init();
        std::memcpy(&p.Version, frame.data, protocol::kAdslFrameBytes);
        REQUIRE(p.check_crc() == 0);
        p.descramble();
        CHECK(p.address() == h.platform().device_addr());
        CHECK(p.alt_m() == h.product().state().own.alt_m);
        CHECK(p.FlightState == 2);
        checked++;
    }
    CHECK(checked > 0);
}

// G6's wiring, not the bucket arithmetic (core/test_timing.cpp already pins
// that down): RadioService owns the deadline, TrafficService the executor's
// report, and this proves the two actually meet in state.timing_stats rather
// than each keeping a private opinion.
TEST_CASE("rf: a completed burst lands in the bench's dwell-phase histogram") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);
    run_on(h, past_settling(h), 6000);

    const timing::SlotTimingStats& stats = h.product().state().timing_stats;
    CHECK(stats.dwell_samples() > 0);
    CHECK(stats.missed() == 0);
    CHECK(stats.refused() == 0);
    // The executor cannot transmit before the deadline it was armed for
    // (§D.3's listen-before-talk only ever pushes the instant later), and it
    // cannot still be waiting once the dwell it was armed inside of has ended,
    // which bounds this well inside one slot's width without pinning a figure
    // on the LBT backoff draw (core/timing/transmit.h kBackoffMinMs..MaxMs)
    // that host virtual time is exercising honestly here.
    CHECK(stats.dwell_worst_us() >= 0);
    CHECK(stats.dwell_worst_us() < timing::kSlot0End * 1000);

    // host::Pps has no jitter model at all: every edge board.h latched is
    // exact, so the whole interval histogram sits in the centre bucket.
    CHECK(stats.pps_samples() > 0);
    CHECK(stats.pps_bucket(3) == stats.pps_samples());
    CHECK(stats.holdover_events() == 0);
}

// The other half of the wiring: whatever already owns the PPS edge
// (hardware/boards) is what the accumulator's holdover count depends on, and
// it has to fire on the transition, not on every pass spent unlocked.
TEST_CASE("rf: losing and regaining PPS through the simulator counts as holdover") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    // host::Pps has no jitter to show, but it does turn over exactly once per
    // simulated second, which is what the first sample needs.
    run_on(h, 0, 1200);
    REQUIRE(h.product().state().timing_stats.pps_samples() >= 1);

    h.world().set_pps_locked(false);
    run_on(h, 1205, 2000);
    h.world().set_pps_locked(true);
    run_on(h, 3210, 2000);

    const timing::SlotTimingStats& stats = h.product().state().timing_stats;
    CHECK(stats.holdover_events() >= 1);
    for (int b = 0; b < timing::SlotTimingStats::kBuckets; b++)
        if (b != 3) CHECK(stats.pps_bucket(b) == 0);
}

TEST_CASE("rf: without an anchored clock nothing is transmitted") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);
    h.world().set_pps_locked(false);
    h.run(4000);
    CHECK(count_of(h.world().air(), simulator::AirEvent::Tx) == 0);
    CHECK(h.product().state().tx_ok == 0);
}

TEST_CASE("rf: on the ground the transmit rate drops to 0.1 Hz") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(0);
    run_on(h, past_settling(h), 8000);
    CHECK(count_of(h.world().air(), simulator::AirEvent::Tx) == 1);
}

// F5. A cold receiver's first solutions walk, and the flight state we derive
// from ground speed decides the transmit rate, so transmitting through that
// window publishes a track nobody flew. gnss::FirstFix has held the answer
// since it was written; until now nothing asked it.
TEST_CASE("rf: nothing goes on air until the first fix has settled") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);

    h.run(gnss::kFirstFixSettleMs - 2000);
    CHECK_FALSE(h.product().state().own.tx_settled);
    CHECK(count_of(h.world().air(), simulator::AirEvent::Tx) == 0);
    CHECK(h.product().state().tx_ok == 0);
    // Not because it has nothing to say: the fix is good and the clock anchored.
    CHECK(h.product().state().own.fix_valid);
    CHECK(h.product().state().clock.pps_locked);

    // And there is one copy of that fact. Own-ship owns the window; the flag on
    // the bus is how the transmit gate reads it, and how anything else that
    // wants the first fix - the chirp on the panel, a status line over the link -
    // reads it too, rather than starting a second watch of its own.
    CHECK(h.product().ownship().first_fix().ever_fixed());
    CHECK_FALSE(h.product().ownship().first_fix().settled(gnss::kFirstFixSettleMs - 2000));

    run_on(h, gnss::kFirstFixSettleMs - 2000, 4000);
    CHECK(h.product().state().own.tx_settled);
    CHECK(h.product().ownship().first_fix().settled(gnss::kFirstFixSettleMs + 2000));
    CHECK(count_of(h.world().air(), simulator::AirEvent::Tx) > 0);
}

// F3. The burst leaves in the direct slot, 450 to 1000 ms into the second, and
// the ADS-L TimeStamp resolves to a quarter of a second. Encoding the fix's own
// second left every transmission claiming quarter zero - an instant 450 ms or
// more before the burst existed - while carrying a position from a third
// instant. This pins the pair together: the quarter the frame claims is the
// quarter it went on air in.
TEST_CASE("rf: the burst is dated when it leaves, and carries the position from then") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(90);
    h.world().set_track_deg(90);
    run_on(h, past_settling(h), 6000);

    const simulator::Air& air = h.world().air();
    int checked = 0;
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& r = air.record(i);
        if (r.event != simulator::AirEvent::Tx) continue;
        protocol::Frame frame{};
        REQUIRE(simulator::Air::framed(r, frame));
        protocol::AdslPacket p{};
        p.init();
        std::memcpy(&p.Version, frame.data, protocol::kAdslFrameBytes);
        REQUIRE(p.check_crc() == 0);
        p.descramble();
        REQUIRE(p.address() == h.platform().device_addr());

        // Inside the direct slot the quarter is 1, 2 or 3. Zero is the old bug.
        REQUIRE(timing::Scheduler::in_direct_slot(r.phase_ms));
        CHECK(int(p.TimeStamp % 4) == r.phase_ms / 250);
        CHECK(int(p.TimeStamp % 4) != 0);
        checked++;
    }
    CHECK(checked >= 4);
}

// F3's quality metric, through the whole pipeline: own-ship predicts the fix it
// last had forward to the instant the next one arrives and keeps the miss. A
// model that has stopped describing the aircraft is then a number on the bench
// rather than a surprise in the air (oss/nrf52-ogn-tracker src/ogn.h:1424-1430).
TEST_CASE("rf: the extrapolation residual is measured against the fix that arrives") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(90);
    h.world().set_track_deg(90);
    h.run(6000);
    CHECK(h.product().state().own.pred_resid_valid);
    CHECK(h.product().state().own.pred_resid_m <= 2);

    // A track that jumps 90 degrees between two solutions is a manoeuvre no
    // constant-turn model saw coming, and the residual says so on the solution
    // that closes the interval - and only on that one, because the model has the
    // aircraft again the moment it flies straight.
    h.world().set_track_deg(180);
    uint16_t worst_m = 0;
    for (uint32_t t = 6000; t <= 6600; t += simulator::Simulator::kStepMs) {
        h.step(t);
        const uint16_t resid_m = h.product().state().own.pred_resid_m;
        if (resid_m > worst_m) worst_m = resid_m;
    }
    CHECK(worst_m > 10);
}
