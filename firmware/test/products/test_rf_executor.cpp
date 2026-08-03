// Below the service list: the SX1262 driver, the host executor that arms and
// services it, and the two decisions they own between them. When the PA may key
// is one - the channel has to be listened to first and the chip's own SetTx
// timeout is what unkeys it - and when the transmit instant is, measured from
// the PPS edge the board latched rather than from a phase that went stale while
// the services ahead of the radio ran. Nothing here is mocked: the chip model
// answers over SPI exactly as the part does, including when it answers nothing
// at all.
#include "core/timing/channel.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/services/traffic.h"
#include "simulator/simulator.h"

using namespace skyblip;

namespace {

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
    hal::Roles roles{
        platform.clock(), rf,       null.link,           null.display, null.kv, null.log_flash,
        null.annunciator, null.dfu, hal::Capability::Rf, 0x0ABBCC};
    runtime::Context context{roles, bus, state};
    go::RadioService radio_service{context};
    go::TrafficService traffic_service{context};

    Status begin() {
        const Status s = rf.begin();
        if (s != Status::Ok) return s;
        state.own.fix_valid = true;
        state.own.utc_valid = true;
        state.own.tx_settled = true;
        state.own.flight_state = 2;
        state.clock.utc_valid = true;
        return radio_service.setup();
    }

    // Exactly what boards/lilygo/t_echo_plus/board.h does at the top of a pass.
    void poll_clock(uint64_t now_us) {
        platform.clock().set_micros(now_us);
        state.clock.pps_locked = platform.pps().locked();
        state.clock.ms_since_pps = platform.pps().ms_since(now_us);
        state.clock.pps_edge_us = platform.pps().last_edge_us();
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
    // Floor plus margin plus 3 dB for every dwell that gave up, and never past
    // the ceiling EN 300 220-2 V3.3.1 §4.6.2.3 puts on it.
    const int asked =
        pass.radio_service.noise_floor().dbm() + timing::NoiseFloor::kClearMarginDb +
        static_cast<int>(pass.radio_service.gave_up_count()) * timing::NoiseFloor::kRetryStepDb;
    const int expected = asked < timing::NoiseFloor::kThresholdCeilingDbm
                             ? asked
                             : timing::NoiseFloor::kThresholdCeilingDbm;
    CHECK(pass.radio_service.lbt_threshold_dbm() == expected);
    CHECK(pass.radio_service.lbt_threshold_dbm() <= timing::NoiseFloor::kThresholdCeilingDbm);
    // A neighbour at -50 dBm asks for far more tolerance than the clause allows,
    // so this site is exactly where the clamp is the reason nothing goes out.
    CHECK(asked > timing::NoiseFloor::kThresholdCeilingDbm);
    // And the threshold in force is on the bus for the companion link to read.
    CHECK(pass.state.carrier_sense_dbm == pass.radio_service.lbt_threshold_dbm());
}

// EN 300 220-2 V3.3.1 §4.6.3.2: the assessment is averaged over at least 160 us.
// One instantaneous read used to decide it, so a burst that happened to be off
// air at that instant licensed a transmission on top of the rest of it.
TEST_CASE("rf: a channel busy for part of the window is busy, whatever the first read said") {
    Pass pass;
    REQUIRE(pass.begin() == Status::Ok);

    // A quiet channel with a neighbour occupying one ninth of every window. Read
    // once at the first instant this is -115 dBm and clear by any threshold;
    // averaged as power it is well above the cold-start threshold of -95 dBm.
    const int8_t window[timing::CarrierSense::kSamples] = {-115, -115, -115, -115, -45,
                                                           -115, -115, -115, -115};
    pass.chip.set_rssi_sequence(window, timing::CarrierSense::kSamples);
    CHECK(window[0] < timing::NoiseFloor::kSeedDbm + timing::NoiseFloor::kClearMarginDb);

    for (uint64_t t = 0; t <= 2900000; t += 10000) pass.whole_pass(t);

    CHECK(pass.state.tx_ok == 0);
    CHECK(pass.radio_service.gave_up_count() >= 2);
    // The floor the policy averages is the window's figure, not the quiet read.
    CHECK(pass.radio_service.noise_floor().dbm() > window[0]);

    CHECK_FALSE(pass.chip.saw_cmd(parts::sx::kSetTx));

    // The same channel read only at the quiet instant keys the PA instead.
    Pass single;
    REQUIRE(single.begin() == Status::Ok);
    single.chip.rssi_dbm = window[0];
    for (uint64_t t = 0; t <= 2900000; t += 10000) single.whole_pass(t);
    CHECK(single.chip.saw_cmd(parts::sx::kSetTx));
    CHECK(single.radio_service.gave_up_count() == 0);
}

// The 30 s no-RX reinitialisation lived in parts::Sx1262::service(), which only
// the host executor ever called: on silicon hardware/platform/zephyr/rf.h had an
// empty service() body, so the radio health watchdog this pins did not exist on
// the device at all. It belongs to whichever thread owns the radio, because
// reinitialising from the service list would put a second writer on the SPI bus
// while a dwell is using it.
TEST_CASE("rf: a receiver that hears nothing is reinitialised by the executor that owns it") {
    models::Sx1262 chip;
    parts::Sx1262 radio(chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin);
    platform::host::Clock clock;
    bus::Queue<messages::RfEvent, 8> events;
    platform::host::Rf rf(radio, clock, events);
    REQUIRE(rf.begin() == Status::Ok);

    hal::RfPlan plan{};
    plan.mode = hal::RfMode::RxMband;
    plan.freq_hz = timing::kMband0Hz;
    plan.start_us = 0;
    plan.end_us = 400000;
    REQUIRE(rf.arm(plan) == Status::Ok);

    const uint32_t deadline_ms = runtime::kRadioNoRxReinitMs;
    for (uint32_t t = 0; t < deadline_ms; t += 10) {
        clock.set_millis(t);
        rf.service(t);
    }
    CHECK(radio.reinit_count() == 0);
    for (uint32_t t = deadline_ms; t <= deadline_ms + 100; t += 10) {
        clock.set_millis(t);
        rf.service(t);
    }
    CHECK(radio.reinit_count() == 1);
    CHECK(radio.mode() == parts::RadioMode::Rx);

    // A frame that arrives restarts the rope rather than shortening it.
    uint8_t burst[protocol::kAdslFrameBytes] = {0};
    chip.queue_rx(burst, sizeof(burst));
    for (uint32_t t = deadline_ms + 100; t <= 2 * deadline_ms; t += 10) {
        clock.set_millis(t);
        rf.service(t);
    }
    CHECK(radio.reinit_count() == 1);
}
