// The settings write through the whole product: a change goes in where a pilot or
// a phone makes it, and what comes out is counted writes on the host KvStore.
// Nothing below the services is stubbed, so the dwell the write has to dodge is
// the one the radio service actually armed.
//
// What is being defended is 1-ARCHITECTURE.md §5.1's "no flash work inside a
// dwell". The settings page lets a pilot change alarm volume and the altimeter
// subscale IN FLIGHT, and on the nRF52840 the store behind hal::KvStore is the
// internal storage_partition: a write there is an NVMC stall on the same core that
// arms PPS-anchored deadlines. Before the page existed a settings write could only
// happen on the ground, because a companion "set" is refused airborne.
#include "core/timing/durable_write.h"
#include "doctest/doctest.h"
#include "test/support/product_rig.h"
#include "ui/input/gesture.h"
#include "ui/screens/settings.h"

using namespace skyblip;

namespace {

// One service pass at a time, so a case can say which millisecond of the second
// the write landed on.
void step_one(Rig& rig, uint32_t& t, uint32_t by_ms = 10) {
    rig.platform.clock().set_millis(t);
    rig.product.step(t);
    t += by_ms;
}

void step_until(Rig& rig, uint32_t& t, uint32_t until_ms) {
    while (t < until_ms) step_one(rig, t);
}

// Stationary timed solutions: core/flight answers OnGround to those, and the UTC
// they carry is what anchors the second the write has to be placed inside. Both
// helpers leave t on a whole second, so a case can name the phase it wants.
void stand_on_the_ground(Rig& rig, uint32_t& t) { rig.seconds(t, 3, /*speed_q=*/0, 0); }

void fly(Rig& rig, uint32_t& t) { rig.seconds(t, 16, /*speed_q=*/200, 1200); }

// The change a pilot makes on the panel, and the one a phone makes over the link,
// arrive at the same flag: comms::ConfigService is the single writer of the blob
// and this is how both editors say the struct moved.
void change_volume(Rig& rig, uint8_t to) {
    rig.state().settings.alarm_volume = to;
    rig.product.config().config().note_settings_changed();
}

uint32_t writes(Rig& rig) { return rig.platform.kv().writes(); }

// The phase of the second the device believes it is at, read off the radio's own
// published view rather than recomputed here.
int published_phase(Rig& rig) { return rig.state().dwell.phase_ms; }

// Runs until the write count moves, and answers the millisecond it moved on.
uint32_t wait_for_write(Rig& rig, uint32_t& t, uint32_t give_up_after_ms) {
    const uint32_t before = writes(rig);
    const uint32_t deadline = t + give_up_after_ms;
    while (t < deadline) {
        step_one(rig, t);
        if (writes(rig) != before) return t;
    }
    return 0;
}

}  // namespace

// The case the finding is about: a value changed with a dwell armed and the
// transmitter allowed on air. The blob must not reach flash there, and when it
// does reach flash the phase it landed on has to be one the policy calls free.
TEST_CASE("flash window: a change made inside a dwell is not written until the window opens") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t);
    step_until(rig, t, t + static_cast<uint32_t>(timing::kDirectStart) + 20);
    REQUIRE(published_phase(rig) >= timing::kDirectStart);
    REQUIRE(rig.state().plan.tx_allowed);
    const uint32_t before = writes(rig);

    change_volume(rig, 4);
    // Through the whole of the dwell it was made in, and the settle behind it,
    // nothing is on flash.
    step_until(rig, t, t + timing::DurableWriteWindow::kSettleMs);
    CHECK(writes(rig) == before);

    const uint32_t at_ms = wait_for_write(rig, t, timing::DurableWriteWindow::kMaxDeferMs);
    REQUIRE(at_ms != 0);
    const int phase = static_cast<int>(at_ms % 1000);
    // One of the two stretches the dwell map leaves: slot 1's tail, or the uplink
    // dwell. Read off the policy rather than restated as numbers.
    CHECK(timing::DurableWriteWindow::free_at(rig.state().plan, phase,
                                              timing::DurableWriteWindow::kWorstWriteMs));
    CHECK(rig.product.config().durable_writes().forced() == 0);
}

// Six taps stepping the alarm volume through six values. One write.
TEST_CASE("flash window: six rapid changes are one write") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t);
    const uint32_t before = writes(rig);

    for (uint8_t volume = 0; volume < 6; volume++) {
        change_volume(rig, volume);
        step_until(rig, t, t + 120);
    }
    REQUIRE(rig.product.config().durable_writes().requests() == 6);

    REQUIRE(wait_for_write(rig, t, timing::DurableWriteWindow::kMaxDeferMs) != 0);
    step_until(rig, t, t + 2000);
    CHECK(writes(rig) - before == 1);
    CHECK(rig.product.config().durable_writes().writes() == 1);

    // And what landed is the last value, not the first: the blob is only read at
    // the instant it is written.
    uint8_t blob[64];
    size_t n = 0;
    REQUIRE(rig.platform.kv().read("settings", blob, sizeof(blob), n) == Status::Ok);
    settings::Settings stored{};
    REQUIRE(settings::from_blob(blob, n, stored) == Status::Ok);
    CHECK(stored.alarm_volume == 5);
}

// The same six values, stepped by a real thumb on the panel rather than by the
// flag underneath it: presses in at the pin the board polls.
TEST_CASE("flash window: a thumb stepping the volume on the panel writes flash once") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    stand_on_the_ground(rig, t);

    for (int i = 0; i < static_cast<int>(go::Page::kCount); i++) {
        if (rig.product.screen().page() == go::Page::Settings) break;
        rig.press(t);
    }
    REQUIRE(rig.product.screen().page() == go::Page::Settings);
    rig.run(t, t + ui::ConfirmGesture::kDoublePressMs + 200);
    t += ui::ConfirmGesture::kDoublePressMs + 200;

    // Down to the volume row: a lone press moves the focus.
    while (rig.product.screen().editor().focus() != ui::SettingsRow::Volume) {
        rig.press(t);
        rig.run(t, t + ui::ConfirmGesture::kDoublePressMs + 100);
        t += ui::ConfirmGesture::kDoublePressMs + 100;
    }

    const uint32_t before = writes(rig);
    const uint8_t started_at = rig.state().settings.alarm_volume;
    // Two presses act on the row, and every further press inside the window acts
    // again: this is what stepping a value looks like to the editor. Six taps walk
    // the volume through all six of its values, which is five accepted changes.
    for (int tap = 0; tap < 6; tap++) {
        rig.press(t);
        rig.run(t, t + 120);
        t += 120;
    }
    REQUIRE(rig.state().settings.alarm_volume != started_at);
    REQUIRE(rig.product.config().durable_writes().requests() == 5);

    rig.run(t, t + 3000);
    t += 3000;
    CHECK(writes(rig) - before == 1);
    CHECK(rig.product.config().durable_writes().forced() == 0);
}

// Whatever phase of the second a change arrives at, it is on flash inside the
// bound - and it is never forced there, because the second offers the window
// twice.
TEST_CASE("flash window: a change is never held past the bound, at any phase") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t);

    uint8_t volume = 0;
    for (int offset = 0; offset < 1000; offset += 70) {
        step_until(rig, t, (t / 1000 + 1) * 1000 + static_cast<uint32_t>(offset));
        const uint32_t asked_at = t;
        volume = static_cast<uint8_t>((volume + 1) % (ui::kMaxAlarmVolume + 1));
        change_volume(rig, volume);
        const uint32_t at_ms = wait_for_write(rig, t, timing::DurableWriteWindow::kMaxDeferMs);
        REQUIRE(at_ms != 0);
        CHECK(at_ms - asked_at <= timing::DurableWriteWindow::kMaxDeferMs);
    }
    CHECK(rig.product.config().durable_writes().forced() == 0);
    CHECK(rig.product.config().durable_writes().worst_wait_ms() <
          timing::DurableWriteWindow::kMaxDeferMs);
}

// A change made on the ground still lands promptly. The dwell map runs on the
// ground too - the receiver is armed and the transmitter still reports at 0.1 Hz -
// so this is not the deferral being switched off, it is the same window being wide
// enough that nobody waits for it.
TEST_CASE("flash window: a change made on the ground is written promptly") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    stand_on_the_ground(rig, t);
    REQUIRE(rig.product.config().config().flight_state() == comms::FlightState::Ground);

    const uint32_t asked_at = t;
    change_volume(rig, 3);
    const uint32_t at_ms = wait_for_write(rig, t, timing::DurableWriteWindow::kMaxDeferMs);
    REQUIRE(at_ms != 0);
    // The settle, and less than one whole second of window on top of it.
    CHECK(at_ms - asked_at >= timing::DurableWriteWindow::kSettleMs);
    CHECK(at_ms - asked_at <= timing::DurableWriteWindow::kSettleMs + 1000);
}

// A value stepped up and back down again is not a change to the blob, and NVS
// charges for a write either way: the sector this policy's whole budget is sized
// against must not fill for nothing.
TEST_CASE("flash window: a change that ends where it started writes no flash") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    stand_on_the_ground(rig, t);
    change_volume(rig, 3);
    REQUIRE(wait_for_write(rig, t, timing::DurableWriteWindow::kMaxDeferMs) != 0);

    const uint32_t before = writes(rig);
    change_volume(rig, 5);
    step_until(rig, t, t + 100);
    change_volume(rig, 3);
    step_until(rig, t, t + 3000);
    CHECK(writes(rig) == before);
    // The request was still served: nothing is left pending, so nothing is waiting
    // for a bound it will never spend.
    CHECK_FALSE(rig.product.config().durable_writes().pending());
}

// The bound answers the cell that dies without warning. A power-off does give
// warning, and once the radio has been aborted the second belongs to nobody, so a
// change still waiting for a window goes down with the rails only if we let it.
TEST_CASE("flash window: a pending change is flushed on the way to power off") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t);
    step_until(rig, t, t + static_cast<uint32_t>(timing::kDirectStart) + 20);
    REQUIRE(rig.state().plan.tx_allowed);

    const uint32_t before = writes(rig);
    change_volume(rig, 1);
    step_one(rig, t);
    REQUIRE(writes(rig) == before);
    REQUIRE(rig.product.config().durable_writes().pending());

    rig.product.shutdown().request(power::ShutdownReason::LinkRequest, t);
    step_one(rig, t);
    REQUIRE(rig.product.shutdown().phase() == power::ShutdownPhase::Parking);
    CHECK(writes(rig) - before == 1);
    CHECK_FALSE(rig.product.config().durable_writes().pending());
    // Not a fault: nothing was armed to disturb.
    CHECK(rig.product.config().durable_writes().forced() == 0);
}

// Failure is loud. The counters go out the door the bench already has open: the
// same Config endpoint dispatch that answers "status" and "timing".
TEST_CASE("flash window: the write counters are readable over the companion link") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    stand_on_the_ground(rig, t);
    change_volume(rig, 2);
    REQUIRE(wait_for_write(rig, t, timing::DurableWriteWindow::kMaxDeferMs) != 0);

    rig.platform.link().clear();
    rig.send("{\"cmd\":\"flash\"}");
    step_until(rig, t, t + 100);
    REQUIRE(rig.platform.link().last_on(messages::Endpoint::Config));
    const std::string reply = rig.platform.link().last().bytes;
    CHECK(reply.find("\"cmd\":\"flash\"") != std::string::npos);
    CHECK(reply.find("\"writes\":1") != std::string::npos);
    CHECK(reply.find("\"forced\":0") != std::string::npos);
    CHECK(reply.find("\"worst_wait_ms\":") != std::string::npos);
    // The policy's own numbers, read back off the device rather than off the source.
    CHECK(reply.find("\"budget_ms\":86") != std::string::npos);
    CHECK(reply.find("\"bound_ms\":3000") != std::string::npos);
}

// E2. Rate limit and coalescing, at the scale the finding names: a companion app
// patching a value per keystroke. A hundred changes have to cost one write, and
// one write is what an erase is charged for - NVS appends until the sector is
// full and then garbage-collects the next one, so the number of blob writes is
// the number the wear budget is spent from (core/timing/durable_write.h sizes one
// against a tERASEPAGE plus the live entries copied forward).
TEST_CASE("flash window: a hundred patches cost one write") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t);
    const uint32_t before = writes(rig);
    REQUIRE(rig.state().settings.alarm_volume == 3);

    // Every pass, which is faster than a thumb and faster than a keystroke, and
    // the whole hundred inside the bound.
    for (int i = 0; i < 100; i++) {
        change_volume(rig, static_cast<uint8_t>((i % 5) + 1));
        step_one(rig, t);
    }
    CHECK(rig.product.config().durable_writes().requests() == 100);
    CHECK(writes(rig) == before);

    REQUIRE(wait_for_write(rig, t, timing::DurableWriteWindow::kMaxDeferMs) != 0);
    step_until(rig, t, t + 2000);
    CHECK(writes(rig) - before == 1);
    CHECK(rig.product.config().durable_writes().writes() == 1);

    // And what one write cost is the hundredth value, not the first.
    uint8_t blob[64];
    size_t n = 0;
    REQUIRE(rig.platform.kv().read("settings", blob, sizeof(blob), n) == Status::Ok);
    settings::Settings stored{};
    REQUIRE(settings::from_blob(blob, n, stored) == Status::Ok);
    CHECK(stored.alarm_volume == 5);
}

// E1 at product scale. The cell is below the warning, so the settings sector is
// not touched at all - and the change is not thrown away either: it stays dirty,
// which is what makes a charger arriving still save it.
TEST_CASE("flash window: a change is held, not written, while the cell is below the warning") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.platform.battery().millivolts = 3400;
    // The board samples the cell once a second and the monitor acts on the third
    // consecutive reading, so a low cell takes four seconds to become a decision.
    rig.seconds(t, 5, /*speed_q=*/0, 0);
    REQUIRE(rig.state().power_level == power::PowerLevel::Low);

    const uint32_t before = writes(rig);
    change_volume(rig, 5);
    step_until(rig, t, t + timing::DurableWriteWindow::kMaxDeferMs + 2000);
    CHECK(writes(rig) == before);
    // Refused, counted once, and never handed to the placement policy - a pending
    // change is one the bound would eventually force onto flash, which is the
    // write this refuses.
    CHECK(rig.product.config().refused_writes() == 1);
    CHECK(rig.product.config().holding_for_power());
    CHECK(rig.product.config().durable_writes().requests() == 0);
    CHECK(rig.product.config().durable_writes().forced() == 0);

    // The cable arrives. The terminal is held above the cell, core/power reports
    // Normal, and the change a pilot made is still there to write.
    rig.platform.battery().external_power = true;
    rig.seconds(t, 3, /*speed_q=*/0, 0);
    REQUIRE(rig.state().power_level == power::PowerLevel::Normal);
    CHECK_FALSE(rig.product.config().holding_for_power());
    CHECK(writes(rig) - before == 1);
    CHECK(rig.product.config().durable_writes().forced() == 0);

    uint8_t blob[64];
    size_t n = 0;
    REQUIRE(rig.platform.kv().read("settings", blob, sizeof(blob), n) == Status::Ok);
    settings::Settings stored{};
    REQUIRE(settings::from_blob(blob, n, stored) == Status::Ok);
    CHECK(stored.alarm_volume == 5);
}

// The one power-off that does not flush, and it is the whole trade: a cell at its
// cutoff takes the flight log record with it and leaves the settings sector
// alone. What is lost is the change a pilot was making; what is protected is
// every change they ever made.
TEST_CASE("flash window: a cell at its cutoff powers off without touching the settings") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.platform.battery().millivolts = 3400;
    rig.seconds(t, 5, /*speed_q=*/0, 0);
    REQUIRE(rig.state().power_level == power::PowerLevel::Low);
    const uint32_t before = writes(rig);

    // A change made on a cell that is already warning: held, never written.
    change_volume(rig, 5);
    rig.seconds(t, 2, /*speed_q=*/0, 0);
    REQUIRE(writes(rig) == before);

    // And now the cell reaches its cutoff, which is the flush this refuses.
    rig.platform.battery().millivolts = 3100;
    rig.seconds(t, 5, /*speed_q=*/0, 0);
    REQUIRE(rig.product.shutdown().reason() == power::ShutdownReason::LowBattery);
    REQUIRE(rig.product.shutdown().phase() != power::ShutdownPhase::Running);
    CHECK(writes(rig) == before);
    CHECK(rig.product.config().refused_writes() == 1);
    CHECK(rig.product.config().holding_for_power());
}

// A deliberate power-off on a healthy cell is the other half of the same rule,
// and it does flush: a pilot who changed a setting and switched the device off
// must not find the old value.
TEST_CASE("flash window: a healthy cell flushes the change it was holding") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t);
    step_until(rig, t, t + static_cast<uint32_t>(timing::kDirectStart) + 20);
    REQUIRE(rig.state().plan.tx_allowed);
    const uint32_t before = writes(rig);

    change_volume(rig, 5);
    step_one(rig, t);
    REQUIRE(writes(rig) == before);

    rig.product.shutdown().request(power::ShutdownReason::LinkRequest, t);
    step_one(rig, t);
    CHECK(writes(rig) - before == 1);
    CHECK(rig.product.config().refused_writes() == 0);
}

// POFCON, through the whole product: the comparator fires in interrupt context on
// the silicon, the product polls the latch once a pass, and the settings writer
// stops. Nothing on this path re-reads the divider.
TEST_CASE("flash window: a fired power-failure comparator stops settings writes") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    stand_on_the_ground(rig, t);
    REQUIRE(rig.state().power_level == power::PowerLevel::Normal);
    REQUIRE(rig.platform.system_power().supply_monitor_armed());

    const uint32_t before = writes(rig);
    rig.platform.system_power().supply_warning = true;
    step_one(rig, t);
    CHECK(rig.product.power().supply_warned());
    CHECK(rig.product.power().supply_warnings() == 1);
    // Read and cleared, so one warning is one warning however many passes run.
    step_until(rig, t, t + 200);
    CHECK(rig.product.power().supply_warnings() == 1);

    change_volume(rig, 5);
    step_until(rig, t, t + timing::DurableWriteWindow::kMaxDeferMs + 2000);
    CHECK(writes(rig) == before);
    // Latching: a rail that came back up does not make it un-happen.
    CHECK_FALSE(rig.product.power().may_write(power::DurableWrite::Settings));
    CHECK(rig.product.power().may_write(power::DurableWrite::FlightRecord));
}
