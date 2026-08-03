// The acceptance invariant on the host: the real product (board, services,
// drivers) links and runs on the host platform with zero framework code. If any
// of it leaks a Zephyr include, this stops compiling.

#include <string>

#include "doctest/doctest.h"
#include "test/support/product_rig.h"

using namespace skyblip;

TEST_CASE("product: setup brings the radio to Rx and reports its capabilities") {
    Rig rig;
    CHECK(rig.setup() == Status::Ok);
    CHECK(rig.state().started);
    CHECK(hal::has(rig.product.capabilities(), hal::Capability::Rf | hal::Capability::Gnss));
    CHECK(rig.product.degraded() == hal::Capability::None);
    CHECK(rig.state().settings.device_addr == 0x0ABBCC);
}

TEST_CASE("product: a missing optional capability is degraded, a required one refuses") {
    constexpr hal::Capabilities kNoBaro = static_cast<hal::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(hal::Capability::Baro));
    Rig degraded(kNoBaro);
    CHECK(degraded.setup() == Status::Ok);
    CHECK(degraded.product.degraded() == hal::Capability::Baro);

    constexpr hal::Capabilities kNoGnss = static_cast<hal::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(hal::Capability::Gnss));
    Rig refused(kNoGnss);
    CHECK(refused.setup() == Status::Down);
    CHECK_FALSE(refused.state().started);
}

TEST_CASE("product: the radio executor is armed against slot deadlines, not polled") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    const uint32_t before = rig.product.radio().arm_count();
    rig.run(0, 3000);
    // Two band changes per UTC second: O-band uplink dwell, then the M-band slots.
    CHECK(rig.product.radio().arm_count() >= before + 4);
}

TEST_CASE("product: step() runs the service cycle deterministically under a modelled clock") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 3000);
    uint8_t pkt[6] = {1, 2, 3, 4, 5, 6};
    rig.platform.chips().radio.queue_rx(pkt, sizeof(pkt));
    rig.run(3000, 4200);
    CHECK(rig.state().rx_bad >= 1);  // too short to be ADS-L: counted, never shown
    CHECK(rig.state().traffic.count() == 0);
}

TEST_CASE("product: the e-paper refreshes on change, not on cadence") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 5000);
    // The self-test page setup() paints, then the first radar frame, both full.
    // The sky then stays static, so nothing else reaches the glass.
    CHECK(rig.platform.chips().epd.present_count == 2);
    CHECK(rig.platform.chips().epd.last_full);

    rig.push_fix(1000, 1);  // fix arrives: the radar page changes
    rig.run(5000, 8000);
    CHECK(rig.platform.chips().epd.present_count == 3);
    CHECK_FALSE(rig.platform.chips().epd.last_full);  // differential, no flash
}

TEST_CASE("product: persisted settings are loaded on setup") {
    Rig rig;
    settings::Settings s = settings::defaults(0x223344);
    s.alarm_volume = 1;
    uint8_t blob[64];
    settings::to_blob(s, blob, sizeof(blob));
    REQUIRE(rig.platform.kv().write("settings", blob, settings::blob_size()) == Status::Ok);

    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.state().settings.alarm_volume == 1);
    CHECK(rig.state().settings.device_addr == 0x223344);
}

TEST_CASE("product: barometric pressure drives vertical speed") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);
    CHECK_FALSE(rig.product.ownship().baro_active());

    rig.push_baro(100000, 1000);
    rig.run(1000, 1000);
    CHECK(rig.product.ownship().baro_active());
    rig.push_baro(101000, 3000);
    rig.run(3000, 3000);

    // +10 m in 2 s = +5 m/s = 40 eighth-m/s.
    CHECK(rig.state().own.climb_e8 == doctest::Approx(40).epsilon(0.05));
}

TEST_CASE("product: a baro sample inside the window is ignored, not extrapolated") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);
    rig.push_baro(100000, 1000);
    rig.run(1000, 1000);
    rig.push_baro(199000, 1100);
    rig.run(1100, 1100);
    CHECK(rig.state().own.climb_e8 == 0);
}

TEST_CASE("product: with no barometer, vertical speed comes from GNSS") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);

    rig.push_fix(1000, 1);
    rig.run(1000, 1000);
    rig.push_fix(1010, 2);
    rig.run(3000, 3000);

    CHECK_FALSE(rig.product.ownship().baro_active());
    CHECK(rig.state().own.climb_e8 == doctest::Approx(40).epsilon(0.05));
}

TEST_CASE("product: once the barometer speaks, GNSS stops setting vertical speed") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);

    rig.push_baro(100000, 500);
    rig.run(500, 500);
    rig.push_baro(100200, 1500);  // +2 m in 1 s = +16 e8
    rig.run(1500, 1500);
    const int16_t from_baro = rig.state().own.climb_e8;
    CHECK(from_baro == doctest::Approx(16).epsilon(0.1));

    rig.push_fix(1000, 1);
    rig.run(2000, 2000);
    rig.push_fix(1200, 2);
    rig.run(4000, 4000);

    CHECK(rig.state().own.climb_e8 == from_baro);
}

TEST_CASE("product: the board reads the cell and the gauge publishes it") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    CHECK_FALSE(rig.state().battery.valid);

    rig.platform.battery().millivolts = 3800;
    rig.run(0, 12000);
    CHECK(rig.state().battery.valid);
    CHECK(rig.state().battery.millivolts == 3800);
    CHECK(rig.state().battery.percent == power::percent_from_mv(3800, false));
    CHECK_FALSE(rig.state().battery.charging);
    CHECK_FALSE(rig.state().battery.external_power);
}

TEST_CASE("product: the same cell on USB power reports a lower state of charge") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.platform.battery().millivolts = 4000;
    rig.run(0, 12000);
    const uint8_t resting = rig.state().battery.percent;

    rig.platform.battery().external_power = true;
    rig.run(12000, 24000);
    CHECK(rig.state().battery.charging);
    CHECK(rig.state().battery.millivolts == 4000);
    CHECK(rig.state().battery.percent < resting);
}

TEST_CASE("product: a board with no battery sense says so instead of reporting empty") {
    constexpr hal::Capabilities kNoBattery = static_cast<hal::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(hal::Capability::Battery));
    Rig rig{kNoBattery};
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.product.degraded() == hal::Capability::Battery);

    rig.run(0, 12000);
    CHECK_FALSE(rig.state().battery.valid);
    CHECK(rig.state().battery.percent == 0);
}

TEST_CASE("product: settings changed over the link are persisted") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.state().settings.alarm_volume = 5;
    // The only way the device can be told it is on the ground: one stationary
    // solution, which core/flight answers OnGround to and the link's gate reads.
    rig.push_fix(/*alt_m=*/0, /*updates=*/1);
    rig.run(0, 100);

    const char* json = "{\"cmd\":\"set\",\"alarm_volume\":2}";
    messages::RxFrame frame{};
    frame.endpoint = messages::Endpoint::Config;
    frame.len = static_cast<uint16_t>(__builtin_strlen(json));
    for (uint16_t i = 0; i < frame.len; i++) frame.data[i] = static_cast<uint8_t>(json[i]);
    rig.platform.link().push_rx(frame);
    rig.run(100, 300);
    rig.product.config().config().confirm();
    // The write is a request now, not a call: it coalesces for
    // DurableWriteWindow::kSettleMs and then waits for a phase of the second the
    // NVMC may stall the core in. Two seconds is several of both.
    rig.run(300, 2400);

    uint8_t blob[64];
    size_t n = 0;
    REQUIRE(rig.platform.kv().read("settings", blob, sizeof(blob), n) == Status::Ok);
    settings::Settings stored{};
    REQUIRE(settings::from_blob(blob, n, stored) == Status::Ok);
    CHECK(stored.alarm_volume == 2);
}

TEST_CASE("product: the reset reason is read once at boot and kept") {
    Rig rig;
    rig.platform.system_power().causes = power::ResetCause::Watchdog | power::ResetCause::Software;
    REQUIRE(rig.setup() == Status::Ok);
    // The bite, not the reset that followed it: that is the whole diagnostic
    // value of the register.
    CHECK(rig.product.reset_reason() == power::ResetReason::Watchdog);

    Rig fresh;
    fresh.platform.system_power().causes = power::ResetCause::PowerOn;
    REQUIRE(fresh.setup() == Status::Ok);
    CHECK(fresh.product.reset_reason() == power::ResetReason::PowerOn);
    // Two different boots must not paint the same page.
    CHECK(fresh.product.boot_page().count_black() != rig.product.boot_page().count_black());
}

// D5. The panel has the reason at boot and then it is gone; the field diagnosis
// happens over the link, days later, with the device in a bag.
TEST_CASE("product: the status reply over the link names why the device came up") {
    Rig rig;
    rig.platform.system_power().causes = power::ResetCause::Watchdog;
    REQUIRE(rig.setup() == Status::Ok);
    rig.platform.link().clear();

    rig.send("{\"cmd\":\"status\"}");
    rig.run(0, 200);
    REQUIRE(rig.platform.link().count_on(messages::Endpoint::Config) == 1);
    CHECK(rig.platform.link().last().bytes.find("WATCHDOG") != std::string::npos);

    // A device that came up because someone pressed the button says that, and
    // not the UNKNOWN a reason nobody passed on would read as.
    Rig pressed;
    pressed.platform.system_power().causes = power::ResetCause::Pin;
    REQUIRE(pressed.setup() == Status::Ok);
    pressed.platform.link().clear();
    pressed.send("{\"cmd\":\"status\"}");
    pressed.run(0, 200);
    CHECK(pressed.platform.link().last().bytes.find("RESET PIN") != std::string::npos);
    CHECK(pressed.platform.link().last().bytes.find("UNKNOWN") == std::string::npos);
}

// B3. The slot map is specified against the PPS edge, and the transmit plan is
// armed from it. An edge rebuilt from the millisecond phase is up to a
// millisecond late on a 5 ms guard.
TEST_CASE("product: the PPS edge on the bus is the edge itself, to the microsecond") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);

    rig.platform.clock().set_micros(12345678);
    rig.product.step(12345);
    CHECK(rig.state().clock.pps_locked);
    CHECK(rig.state().clock.pps_edge_us == 12000000u);
    CHECK(rig.state().clock.ms_since_pps == 345u);

    // What the same pass used to publish, rebuilt from the phase: the sub-
    // millisecond remainder was thrown away, and it is not a rounding error but
    // a signed lateness on every plan armed from it.
    const uint64_t rebuilt =
        12345678u - static_cast<uint64_t>(rig.state().clock.ms_since_pps) * 1000;
    CHECK(rebuilt - rig.state().clock.pps_edge_us == 678u);

    // No lock, no edge: a stale instant is worse than none, because the phase
    // it implies is a plausible one.
    rig.platform.pps().set_locked(false);
    rig.platform.clock().set_micros(13345678);
    rig.product.step(13345);
    CHECK_FALSE(rig.state().clock.pps_locked);
    CHECK(rig.state().clock.pps_edge_us == 0u);
}

TEST_CASE("product: the first fix is announced once, then own-ship settles before it flies") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 500);
    CHECK_FALSE(rig.product.ownship().first_fix().ever_fixed());
    CHECK(int(rig.platform.annunciator().level()) == 0);

    rig.push_fix(1000, 1);
    rig.run(550, 700);
    CHECK(rig.product.ownship().first_fix().ever_fixed());
    CHECK(int(rig.platform.annunciator().level()) == 1);  // the chirp, at the lowest step
    // The motor stays out of it: haptics mean traffic that escalated.
    CHECK(rig.platform.annunciator().vibro_ms() == 0);

    // It is short and it stops by itself, so nothing has to remember to silence
    // it before the first traffic contact arrives.
    rig.run(750, 2000);
    CHECK(int(rig.platform.annunciator().level()) == 0);

    // Nothing is worth transmitting yet, and 20 s later it is.
    const gnss::FirstFix& fix = rig.product.ownship().first_fix();
    CHECK_FALSE(fix.settled(2000));
    CHECK_FALSE(fix.settled(fix.fix_since_ms() + gnss::kFirstFixSettleMs - 1));
    CHECK(fix.settled(fix.fix_since_ms() + gnss::kFirstFixSettleMs));

    // A second acquisition is not a first one: no second chirp, and the shorter
    // wait applies.
    gnss::GnssFix lost{};
    lost.valid = false;
    rig.product.bus().gnss.push(lost);
    rig.run(2050, 2200);
    CHECK_FALSE(fix.settled(2200));
    rig.push_fix(1000, 2);
    rig.run(2250, 2400);
    CHECK(int(rig.platform.annunciator().level()) == 0);
    CHECK(fix.settled(fix.fix_since_ms() + gnss::kRefixSettleMs));
}
