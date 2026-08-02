// What is on the 868 MHz air, and when. Every burst in these tests is a real
// scrambled ADS-L frame put on the channel at an absolute instant. The radio
// hears it only if the firmware had the receiver tuned there at that moment.
// A slot-map regression shows up as DEAF records, not as a missing feature.
#include <cstring>
#include <string>

#include "core/timing/channel.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/services/traffic.h"
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

int count_of(const simulator::Air& air, simulator::AirEvent want) {
    int n = 0;
    for (int i = 0; i < air.record_count(); i++)
        if (air.record(i).event == want) n++;
    return n;
}

// The radio policy and its executor with the board's job done by hand, so that
// the two halves of a service pass - the instant the PPS phase is read off the
// hardware, and the instant the policy uses it - can be pulled apart. On silicon
// they are separated by however long the services ahead of the radio took.
// Every part below the services is the real one: the driver, the chip model, the
// executor and the service that turns radio events into counters.
struct Pass {
    platform::host::Platform platform{};
    models::Sx1262& chip{platform.chips().radio};
    parts::Sx1262 radio{chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin};
    bus::Bus bus{};
    bus::State state{};
    platform::host::Rf rf{radio, platform.clock(), bus.rf};
    runtime::NullRoles null{};
    hal::Roles roles{platform.clock(), rf,       null.link,           null.display, null.kv,
                     null.annunciator, null.dfu, hal::Capability::Rf, 0x0ABBCC};
    runtime::Context context{roles, bus, state};
    go::RadioService radio_service{context};
    go::TrafficService traffic_service{context};

    Status begin() {
        const Status s = rf.begin();
        if (s != Status::Ok) return s;
        state.own.fix_valid = true;
        state.own.utc_valid = true;
        state.own.flight_state = 2;
        state.clock.utc_valid = true;
        return radio_service.setup();
    }

    // Exactly what boards/lilygo/t_echo_plus/board.h does at the top of a pass.
    void poll_clock(uint64_t now_us) {
        platform.clock().set_micros(now_us);
        state.clock.pps_locked = platform.pps().locked();
        state.clock.ms_since_pps = platform.pps().ms_since(now_us);
        state.clock.pps_edge_us = now_us - static_cast<uint64_t>(state.clock.ms_since_pps) * 1000;
    }

    void whole_pass(uint64_t now_us) {
        poll_clock(now_us);
        services_at(now_us);
    }

    void services_at(uint64_t now_us) {
        platform.clock().set_micros(now_us);
        const uint32_t now_ms = static_cast<uint32_t>(now_us / 1000);
        state.own.utc = now_ms / 1000;
        state.own.fix_ms = now_ms;
        rf.service(now_ms);
        radio_service.tick(now_ms);
        traffic_service.tick(now_ms);
    }

    // The executor between service passes: it runs on its own thread on silicon,
    // so it sees time the service list does not.
    void executor_until(uint64_t until_us, uint64_t step_us = 100) {
        for (uint64_t t = platform.clock().micros() + step_us; t <= until_us; t += step_us) {
            platform.clock().set_micros(t);
            rf.service(static_cast<uint32_t>(t / 1000));
            if (chip.tx_pending && first_tx_us == 0) first_tx_us = t;
        }
    }

    uint64_t first_tx_us{0};
};

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
    h.run(6000);

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
    CHECK(h.product().radio().duty_permille(6000) < timing::AirTime::kLimitPermille);
    CHECK_FALSE(h.product().radio().over_budget());
}

TEST_CASE("rf: what own-ship put on air decodes back to own-ship state") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);
    h.world().set_altitude_m(1200);
    h.run(3000);

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
    h.run(8000);
    CHECK(count_of(h.world().air(), simulator::AirEvent::Tx) == 1);
}

// §D.3: the executor samples the carrier and holds the burst while the channel
// is busy. The dwell's end is what gives up, and it says so.
TEST_CASE("rf: listen before talk holds a burst on a busy channel") {
    models::Sx1262 chip;
    parts::Sx1262 radio(chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin);
    platform::host::Clock clock;
    bus::Queue<messages::RfEvent, 8> events;
    platform::host::Rf rf(radio, clock, events);
    REQUIRE(rf.begin() == Status::Ok);

    const uint8_t frame[protocol::AdslPacket::kTxBytes] = {0x72, 0x4B};
    hal::RfPlan plan{};
    plan.mode = hal::RfMode::RxMband;
    plan.freq_hz = timing::kMband0Hz;
    plan.start_us = 450000;
    plan.end_us = 795000;
    plan.tx = frame;
    plan.tx_len = sizeof(frame);
    plan.tx_at_us = 600000;
    plan.lbt = true;

    chip.rssi_dbm = -50;  // a neighbour holding the channel
    REQUIRE(rf.arm(plan) == Status::Ok);
    for (uint32_t t = 450; t <= 800; t += 5) {
        clock.set_millis(t);
        rf.service(t);
    }
    CHECK_FALSE(chip.tx_pending);
    CHECK(radio.mode() == parts::RadioMode::Rx);
    messages::RfEvent e{};
    bool busy_reported = false;
    while (events.pop(e))
        if (e.type == messages::RfEventType::TxBusy) busy_reported = true;
    CHECK(busy_reported);

    // The same dwell one second later, on a quiet channel.
    chip.rssi_dbm = simulator::Air::kNoiseFloorDbm;
    plan.start_us += 1000000;
    plan.end_us += 1000000;
    plan.tx_at_us += 1000000;
    REQUIRE(rf.arm(plan) == Status::Ok);
    for (uint32_t t = 1450; t <= 1800; t += 5) {
        clock.set_millis(t);
        rf.service(t);
    }
    CHECK(radio.mode() == parts::RadioMode::Tx);
    CHECK(chip.tx_pending);
    // Two hundred milliseconds in Tx with no TxDone, and no firmware timer
    // anywhere: see the case below for why that is the right answer.
    CHECK(chip.tx_timeout_ticks != 0);
}

// A7, decided and written down: there is no second, firmware-side transmit
// watchdog in the executor or in the driver's service, and there does not need
// to be one. SetTx carries a timeout of air time plus 25 ms (DS 13.4.1), so the
// hardware unkeys the PA on its own and no firmware timer can be faster; poll()
// turns that into a counted recovery. A backstop could only run on the thread
// that owns the radio, which is the same loop that already reads the IRQ, so it
// would fire strictly after the chip does. The one gap it might have covered - a
// burst issued so late that the dwell closes before the chip's timeout expires -
// is closed by the next dwell, which is standby-bracketed (DS 13.1) and unkeys
// the transmitter on its way to the new channel.
TEST_CASE("rf: the SetTx timeout is the transmit watchdog, and the next dwell is the backstop") {
    models::Sx1262 chip;
    parts::Sx1262 radio(chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin);
    REQUIRE(radio.begin() == Status::Ok);
    parts::MbandConfig cfg{};
    cfg.freq_hz = timing::kMband0Hz;
    cfg.sync = protocol::kSharedSync;
    cfg.sync_bits = protocol::kSharedSyncBits;
    cfg.payload_bytes = protocol::kRxChipBytes;
    REQUIRE(radio.configure_mband(cfg) == Status::Ok);
    radio.start_receive();

    uint8_t frame[protocol::kAdslFrameBytes] = {0};
    REQUIRE(radio.transmit(frame, sizeof(frame)) == Status::Ok);
    // The chip is holding a timer that expires well inside one 400 ms dwell.
    const uint32_t timeout_ms = chip.tx_timeout_ticks * parts::sx::kTimeoutStepNs / 1000000u;
    CHECK(timeout_ms > 0);
    CHECK(timeout_ms < timing::kSlot0End - timing::kSlot0Start);

    // And a TxDone that never came and a timeout nobody polled still leave the
    // transmitter off before the next channel opens.
    cfg.freq_hz = timing::kMband1Hz;
    REQUIRE(radio.configure_mband(cfg) == Status::Ok);
    CHECK(chip.standby);
    CHECK_FALSE(chip.receiving);
    CHECK(radio.mode() == parts::RadioMode::Standby);
    CHECK(chip.fault == models::Sx1262::Fault::None);
}

// A8. Capability::Rf used to be ORed in unconditionally, and bring-up only
// proved BUSY went low - which an empty footprint with a pull-down does too. A
// radio that is not there then boots as a radio that is simply never hearing
// anything, which is the hardest fault on this board to diagnose in the field.
TEST_CASE("rf: a radio that will not answer over SPI is an absent capability, not a quiet one") {
    platform::host::Platform fitted_platform;
    go::Product<platform::host::Platform> fitted{fitted_platform};
    CHECK(hal::has(fitted.capabilities(), hal::Capability::Rf));
    CHECK(fitted.setup() == Status::Ok);
    CHECK(fitted.flyable());

    platform::host::Platform dead_platform;
    dead_platform.chips().radio.miso_dead = true;
    go::Product<platform::host::Platform> dead{dead_platform};
    CHECK_FALSE(hal::has(dead.capabilities(), hal::Capability::Rf));
    // The radio is required, so the loop refuses to fly - and the self-test page
    // that names the part is painted before anything refuses.
    CHECK(dead.setup() == Status::Down);
    CHECK_FALSE(dead.flyable());
    CHECK(hal::has(hal::missing(dead.capabilities(), go::kRequired), hal::Capability::Rf));
}

// B3. The phase used to be whatever the board sampled at the top of the pass,
// combined with a clock read at the bottom of it. Everything the service list
// does in between - parsing a GNSS burst, painting a panel - moved the transmit
// instant that far late, on top of the 5 ms the jitter guard already allows for.
TEST_CASE("rf: the transmit instant is measured from the latched edge, not from a stale phase") {
    for (uint32_t lag_ms : {0u, 4u, 9u}) {
        Pass pass;
        REQUIRE(pass.begin() == Status::Ok);

        // A real edge is not on a millisecond boundary either.
        const uint64_t edge_us = 4000000;
        pass.poll_clock(edge_us + 460450);
        const uint64_t at_service_us = edge_us + 460450 + lag_ms * 1000;
        const uint32_t now_ms = static_cast<uint32_t>(at_service_us / 1000);
        pass.services_at(at_service_us);

        const timing::SlotPlan plan = timing::Scheduler{}.plan(500, pass.state.clock);
        const timing::Transmitter::Attempt wanted =
            pass.radio_service.transmitter().attempt(plan, 4, now_ms, true, 0);
        REQUIRE(wanted.go);
        const uint64_t wanted_us = edge_us + static_cast<uint64_t>(wanted.at_ms) * 1000;

        pass.executor_until(edge_us + 800000);
        REQUIRE(pass.first_tx_us != 0);
        const int64_t error_us =
            static_cast<int64_t>(pass.first_tx_us) - static_cast<int64_t>(wanted_us);
        CAPTURE(lag_ms);
        CAPTURE(error_us);
        CHECK(error_us >= 0);
        CHECK(error_us < timing::kJitterGuardMs * 1000);
        // And in fact inside the millisecond the PPS surface is quantised to,
        // whatever the pass costs: the edge is an instant, not a phase.
        CHECK(error_us < 1000);
    }
}

// E1. A site where the carrier is never under a fixed -90 dBm used to be a
// device that transmitted nothing and reported nothing about why.
TEST_CASE("rf: a channel that is never clear is counted, and each refusal buys 3 dB") {
    Pass pass;
    REQUIRE(pass.begin() == Status::Ok);
    // The threshold before anything has been measured is OGN's seed plus its
    // margin, so a cold start on a quiet channel is not paralysed either.
    CHECK(pass.radio_service.lbt_threshold_dbm() ==
          timing::NoiseFloor::kSeedDbm + timing::NoiseFloor::kClearMarginDb);

    pass.chip.rssi_dbm = -50;  // a neighbour sitting on the channel
    // Short of D.3's forced transmission at 3000 ms, so nothing here is on air
    // because the rules gave up on listening.
    for (uint64_t t = 0; t <= 2900000; t += 10000) pass.whole_pass(t);

    CHECK(pass.state.tx_ok == 0);
    CHECK(pass.radio_service.gave_up_count() >= 2);
    CHECK(pass.state.tx_busy == pass.radio_service.gave_up_count());
    // The floor is a measurement now, and it has moved off the seed.
    CHECK(pass.radio_service.noise_floor().samples() > 3);
    CHECK(pass.radio_service.noise_floor().dbm() > timing::NoiseFloor::kSeedDbm);
    // Floor plus margin plus 3 dB for every dwell that gave up.
    const int expected =
        pass.radio_service.noise_floor().dbm() + timing::NoiseFloor::kClearMarginDb +
        static_cast<int>(pass.radio_service.gave_up_count()) * timing::NoiseFloor::kRetryStepDb;
    CHECK(pass.radio_service.lbt_threshold_dbm() == expected);
}
