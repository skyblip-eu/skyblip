// One terminal voltage, two meanings: on the cable the charger holds the cell
// above its own resting voltage, so the same reading is a far emptier cell. These
// pin down that the gauge says which curve it read, never walks the wrong way, and
// ignores the sag of a 22 dBm burst. A percentage that jumps when the radio keys
// is a gauge a pilot stops believing.
#include <string>

#include "core/power/battery.h"
#include "core/power/cutoff.h"
#include "core/power/reset_reason.h"
#include "core/power/shutdown.h"
#include "core/power/wake.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::power;

static messages::BatterySample sample(uint16_t millivolts, bool external_power = false) {
    return messages::BatterySample{millivolts, external_power};
}

// The gauge medians three readings, so a settled value takes three - which is the
// same reason a single transmit burst cannot move the needle.
static void settle(Gauge& gauge, uint16_t millivolts, bool external_power = false) {
    for (int i = 0; i < 4; i++) gauge.apply(sample(millivolts, external_power));
}

TEST_CASE("battery: the curve is monotonic and clamps at both ends") {
    CHECK(percent_from_mv(3000, false) == 0);
    CHECK(percent_from_mv(kEmptyMv, false) == 0);
    CHECK(percent_from_mv(kFullMv, false) == 100);
    CHECK(percent_from_mv(4500, false) == 100);

    uint8_t previous = 0;
    for (uint16_t mv = kEmptyMv; mv <= kFullMv; mv += 10) {
        const uint8_t percent = percent_from_mv(mv, false);
        CHECK(percent >= previous);
        previous = percent;
    }
}

// The point of the whole module: the same reading means two different states of
// charge, because on charge the terminal sits above the cell's own voltage.
TEST_CASE("battery: charging reads lower than resting at the same voltage") {
    for (uint16_t mv = 3600; mv <= 4150; mv += 50)
        CHECK(percent_from_mv(mv, true) < percent_from_mv(mv, false));

    // 4.00 V is nearly full on the bench and barely half full with a charger on it.
    CHECK(percent_from_mv(4000, false) == 89);
    CHECK(percent_from_mv(4000, true) == 55);
}

TEST_CASE("battery: the flat middle of the cell is not a straight line") {
    // 3.70 -> 3.80 V is a quarter of the capacity; 4.10 -> 4.20 V is a twentieth.
    const int mid = percent_from_mv(3800, false) - percent_from_mv(3700, false);
    const int top = percent_from_mv(4200, false) - percent_from_mv(4100, false);
    CHECK(mid > top);
}

TEST_CASE("gauge: nothing is reported before the first reading") {
    Gauge gauge;
    CHECK_FALSE(gauge.state().valid);
    CHECK(gauge.state().millivolts == 0);

    gauge.apply(sample(3900));
    CHECK(gauge.state().valid);
    // The first reading stands on its own rather than being mixed with zeroes.
    CHECK(gauge.state().millivolts == 3900);
    CHECK(gauge.state().percent == percent_from_mv(3900, false));
    CHECK_FALSE(gauge.state().charging);
}

TEST_CASE("gauge: one transmit burst does not move the gauge") {
    Gauge gauge;
    settle(gauge, 3900);
    const uint8_t before = gauge.state().percent;

    // A 22 dBm burst sags the rail for a single reading. The median throws it out
    // whole: 200 mV of transient must not read as a fifth of the pack gone.
    gauge.apply(sample(3700));
    CHECK(gauge.state().millivolts == 3900);
    CHECK(gauge.state().percent == before);

    // Two readings in a row are the cell, not a burst.
    gauge.apply(sample(3700));
    CHECK(gauge.state().millivolts == 3700);
    CHECK(gauge.state().percent < before);
}

TEST_CASE("gauge: the percentage only moves the way the current flows") {
    Gauge gauge;
    settle(gauge, 3800);
    const uint8_t discharging = gauge.state().percent;

    // Noise upwards while running on the cell is noise, not charge: two readings
    // to get past the median, and the percentage still does not climb.
    gauge.apply(sample(3830));
    gauge.apply(sample(3830));
    CHECK(gauge.state().millivolts == 3830);
    CHECK(gauge.state().percent == discharging);

    // Draining further does move it.
    settle(gauge, 3700);
    CHECK(gauge.state().percent < discharging);
}

TEST_CASE("gauge: plugging in re-seats on the charge curve, unplugging on the other") {
    Gauge gauge;
    settle(gauge, 4000);
    const uint8_t resting = gauge.state().percent;
    CHECK(resting == percent_from_mv(4000, false));

    // Same cell, cable in: the charger is pushing, so the honest number drops.
    settle(gauge, 4000, /*external_power=*/true);
    CHECK(gauge.state().charging);
    CHECK(gauge.state().external_power);
    CHECK(gauge.state().percent == percent_from_mv(4000, true));
    CHECK(gauge.state().percent < resting);

    // Cable out: the charge current stops, the cell relaxes, and the gauge is
    // allowed to jump back up rather than being held down by the old curve.
    settle(gauge, 4000, /*external_power=*/false);
    CHECK_FALSE(gauge.state().charging);
    CHECK(gauge.state().percent == resting);
}

TEST_CASE("gauge: charging never walks backwards") {
    Gauge gauge;
    settle(gauge, 3900, /*external_power=*/true);
    const uint8_t climbing = gauge.state().percent;

    gauge.apply(sample(3870, /*external_power=*/true));
    CHECK(gauge.state().percent == climbing);

    settle(gauge, 4100, /*external_power=*/true);
    CHECK(gauge.state().percent > climbing);
}

TEST_CASE("gauge: a full cell on the cable is charged, not charging") {
    Gauge gauge;
    settle(gauge, kFullMv, /*external_power=*/true);
    CHECK(gauge.state().external_power);
    // No charge current can be measured, so the float plateau is the signal that
    // the charger has finished.
    CHECK(gauge.state().millivolts >= kChargeCompleteMv);
    CHECK_FALSE(gauge.state().charging);
    CHECK(gauge.state().percent == 100);
}

// The acting half. The gauge above says what the cell holds; this says when that
// number stops being a reading and becomes a decision. SoftRF's shape, because
// it is the one that survived contact with the same pack on the same board:
// 3.5 V warns, 3.2 V cuts off, three consecutive samples before either, and a
// 1.8 V floor under both so an unconnected divider cannot switch the device off
// in someone's hand.

TEST_CASE("cutoff: two samples below the cutoff are not enough, the third acts") {
    // Under the rule, two samples claim nothing at all: the monitor has not yet
    // separated a real cell from a burst sagging the rail.
    CutoffMonitor monitor;
    CHECK(monitor.apply(sample(3100)) == PowerLevel::Unknown);
    CHECK(monitor.apply(sample(3100)) == PowerLevel::Unknown);
    CHECK_FALSE(monitor.cutoff());
    CHECK(monitor.apply(sample(3100)) == PowerLevel::Cutoff);
    CHECK(monitor.cutoff());
}

TEST_CASE("cutoff: one good sample resets the count, so a transmit sag cannot act") {
    CutoffMonitor monitor;
    monitor.apply(sample(3100));
    monitor.apply(sample(3100));
    monitor.apply(sample(3800));  // the burst ended, the rail came back
    CHECK(monitor.below_cutoff() == 0);
    CHECK(monitor.level() == PowerLevel::Normal);

    monitor.apply(sample(3100));
    monitor.apply(sample(3100));
    CHECK_FALSE(monitor.cutoff());
}

TEST_CASE("cutoff: both thresholds are boundaries, not ranges") {
    // Exactly at the warning: still normal. One millivolt under: warned.
    CutoffMonitor at_warn;
    for (int i = 0; i < 4; i++) at_warn.apply(sample(kLowWarnMv));
    CHECK(at_warn.level() == PowerLevel::Normal);

    CutoffMonitor under_warn;
    for (int i = 0; i < 4; i++) under_warn.apply(sample(kLowWarnMv - 1));
    CHECK(under_warn.level() == PowerLevel::Low);

    // Exactly at the cutoff: warned, not cut off. One millivolt under: cut off.
    CutoffMonitor at_cutoff;
    for (int i = 0; i < 4; i++) at_cutoff.apply(sample(kCutoffMv));
    CHECK(at_cutoff.level() == PowerLevel::Low);
    CHECK_FALSE(at_cutoff.cutoff());

    CutoffMonitor under_cutoff;
    for (int i = 0; i < 4; i++) under_cutoff.apply(sample(kCutoffMv - 1));
    CHECK(under_cutoff.cutoff());
}

TEST_CASE("cutoff: the warning comes before the cutoff, never instead of it") {
    CutoffMonitor monitor;
    for (int i = 0; i < 3; i++) monitor.apply(sample(3400));
    CHECK(monitor.level() == PowerLevel::Low);
    CHECK(monitor.warned());
    CHECK_FALSE(monitor.cutoff());

    for (int i = 0; i < 3; i++) monitor.apply(sample(3100));
    CHECK(monitor.cutoff());
}

TEST_CASE("cutoff: a floating ADC cannot power the device off") {
    CutoffMonitor monitor;
    for (int i = 0; i < 20; i++) monitor.apply(sample(kImplausibleFloorMv));
    CHECK_FALSE(monitor.cutoff());
    CHECK(monitor.level() == PowerLevel::Unknown);
    CHECK(monitor.implausible() == 20);

    // And a divider that reads zero is the same case, not an empty cell.
    CutoffMonitor disconnected;
    for (int i = 0; i < 20; i++) disconnected.apply(sample(0));
    CHECK_FALSE(disconnected.cutoff());

    // One millivolt above the floor is a reading again, and it does act.
    CutoffMonitor believable;
    for (int i = 0; i < 3; i++) believable.apply(sample(kImplausibleFloorMv + 1));
    CHECK(believable.cutoff());
}

TEST_CASE("cutoff: nothing acts on a terminal the charger is holding up") {
    CutoffMonitor monitor;
    for (int i = 0; i < 10; i++) monitor.apply(sample(3100, /*external_power=*/true));
    CHECK(monitor.level() == PowerLevel::Normal);
    CHECK_FALSE(monitor.cutoff());
}

TEST_CASE("cutoff: the decision latches once it is made") {
    CutoffMonitor monitor;
    for (int i = 0; i < 3; i++) monitor.apply(sample(3100));
    REQUIRE(monitor.cutoff());
    // The radio going quiet lets the cell relax upwards. It is still empty.
    for (int i = 0; i < 10; i++) monitor.apply(sample(4000));
    CHECK(monitor.cutoff());
}

// Three things ask for the rails to drop and all three take the same road. The
// order matters more than the entry: the radio and the panel are parked first,
// and the wake pin is armed only once the button is actually up.

TEST_CASE("shutdown: a short press is not a shutdown, a long one is") {
    ShutdownSequencer seq;
    uint32_t t = 0;
    seq.tick(t, false);  // the button starts up: the hold is armed

    for (t = 10; t < 500; t += 10) seq.tick(t, true);
    seq.tick(t, false);
    CHECK(seq.phase() == ShutdownPhase::Running);
    CHECK(seq.reason() == ShutdownReason::None);

    for (t = 1000; t < 1000 + kLongPressMs + 100; t += 10) seq.tick(t, true);
    CHECK(seq.phase() == ShutdownPhase::Parking);
    CHECK(seq.reason() == ShutdownReason::LongPress);
}

TEST_CASE("shutdown: the wake pin waits for the button to come up") {
    ShutdownSequencer seq;
    uint32_t t = 0;
    seq.tick(t, false);
    for (t = 10; t <= 10 + kLongPressMs; t += 10) seq.tick(t, true);
    REQUIRE(seq.phase() == ShutdownPhase::Parking);

    // The panel needs its full refresh before the rails may go.
    for (; t < 10 + kLongPressMs + kParkMs; t += 10) seq.tick(t, true);
    CHECK(seq.phase() == ShutdownPhase::Parking);

    // Parked, but the finger is still on the button: a level-sensed wake pin
    // armed now brings the device straight back up.
    for (; t < 30000; t += 10) seq.tick(t, true);
    CHECK(seq.phase() == ShutdownPhase::AwaitRelease);
    CHECK_FALSE(seq.ready_to_power_off());

    const uint32_t released = t;
    for (; t < released + kReleaseSettleMs; t += 10) seq.tick(t, false);
    CHECK_FALSE(seq.ready_to_power_off());
    seq.tick(t + kReleaseSettleMs, false);
    CHECK(seq.phase() == ShutdownPhase::Off);
    CHECK(seq.ready_to_power_off());
}

TEST_CASE("shutdown: a device woken by the button does not switch itself off again") {
    // SYSTEM OFF is left by a press, so the first thing the sequencer ever sees
    // is a button that is already down. Counting that as a hold powers the
    // device off before the panel has drawn a single frame.
    ShutdownSequencer seq;
    for (uint32_t t = 0; t < 10000; t += 10) seq.tick(t, true);
    CHECK(seq.phase() == ShutdownPhase::Running);

    // Once it has been released, the next hold counts.
    uint32_t t = 10000;
    seq.tick(t, false);
    for (; t <= 10000 + kLongPressMs + 10; t += 10) seq.tick(t, true);
    CHECK(seq.phase() == ShutdownPhase::Parking);
}

TEST_CASE("shutdown: low battery and the link take the same road as the button") {
    for (const ShutdownReason reason : {ShutdownReason::LowBattery, ShutdownReason::LinkRequest}) {
        ShutdownSequencer seq;
        seq.request(reason, 0);
        CHECK(seq.phase() == ShutdownPhase::Parking);
        CHECK(seq.reason() == reason);
        CHECK(seq.going_down());

        uint32_t t = 0;
        for (; t < kParkMs + kReleaseSettleMs + 100; t += 10) seq.tick(t, false);
        CHECK(seq.ready_to_power_off());
    }
}

TEST_CASE("shutdown: nothing cancels a shutdown once it has started") {
    ShutdownSequencer seq;
    seq.request(ShutdownReason::LowBattery, 0);
    seq.request(ShutdownReason::LinkRequest, 10);
    CHECK(seq.reason() == ShutdownReason::LowBattery);

    for (uint32_t t = 0; t < 20000; t += 10) seq.tick(t, false);
    CHECK(seq.phase() == ShutdownPhase::Off);
    // Even a fresh press cannot bring it back.
    seq.tick(20000, true);
    CHECK(seq.phase() == ShutdownPhase::Off);
}

TEST_CASE("shutdown: the hold is readable while it fills up") {
    ShutdownSequencer seq;
    seq.tick(0, false);
    CHECK(seq.held_ms(0) == 0);
    seq.tick(100, true);
    seq.tick(600, true);
    CHECK(seq.held_ms(600) == 500);
    seq.tick(700, false);
    CHECK(seq.held_ms(700) == 0);
}

// M. The whole sequencer across the 49.7-day wrap of hal::Clock::millis(): the
// long press, the park, and the settle after the button comes up. It is written as
// unsigned differences from a stamp guarded by a flag, and this is the case that
// keeps it that way. What the two failures would be: a press that never reaches
// two seconds, so the device cannot be switched off at all until the counter comes
// round; or a park that expires the instant it starts, so the rails go while the
// panel is still refreshing.
TEST_CASE("shutdown: the press, the park and the settle span the 49.7-day wrap") {
    ShutdownSequencer seq;
    const uint32_t before = 0xFFFFFF00u;  // 256 ms short of the wrap
    uint32_t t = before;
    seq.tick(t, false);  // the button starts up: the hold is armed

    // Pressed 256 ms before the wrap, held through it. The hold reads as elapsed
    // time on both sides of zero.
    seq.tick(t, true);
    CHECK(seq.held_ms(before + 500u) == 500);
    for (t = before; t != before + kLongPressMs - 10u; t += 10) seq.tick(t, true);
    CHECK(seq.phase() == ShutdownPhase::Running);
    seq.tick(before + kLongPressMs, true);
    CHECK(seq.phase() == ShutdownPhase::Parking);
    CHECK(seq.reason() == ShutdownReason::LongPress);

    // The park is three seconds of panel time, not zero and not seven weeks.
    const uint32_t parking_since = before + kLongPressMs;
    seq.tick(parking_since + kParkMs - 1u, true);
    CHECK(seq.phase() == ShutdownPhase::Parking);
    seq.tick(parking_since + kParkMs, true);
    CHECK(seq.phase() == ShutdownPhase::AwaitRelease);

    // The finger comes up, and the settle before the wake pin is armed is
    // measured the same way.
    const uint32_t released = parking_since + kParkMs + 40u;
    seq.tick(released, false);
    seq.tick(released + kReleaseSettleMs - 1u, false);
    CHECK_FALSE(seq.ready_to_power_off());
    seq.tick(released + kReleaseSettleMs, false);
    CHECK(seq.phase() == ShutdownPhase::Off);
    CHECK(seq.ready_to_power_off());
}

// The order the rails are allowed to collapse in. Nothing here is about WHEN
// the device switches off - the sequencer above owns that - and everything is
// about what is still powered when each command is sent. Three of these steps
// are silently useless one position later.

namespace {

class RecordingSink : public power::PowerDownSink {
   public:
    void perform(power::PowerDownStep step) override {
        if (count < power::kPowerDownStepCount) steps[count++] = step;
    }
    int at(power::PowerDownStep step) const {
        for (int i = 0; i < count; i++)
            if (steps[i] == step) return i;
        return -1;
    }
    power::PowerDownStep steps[power::kPowerDownStepCount]{};
    int count{0};
};

}  // namespace

TEST_CASE("power down: every step runs once, in the order the table declares") {
    RecordingSink sink;
    power_down(sink);
    REQUIRE(sink.count == kPowerDownStepCount);
    for (int i = 0; i < kPowerDownStepCount; i++) {
        CHECK(sink.steps[i] == kPowerDownOrder[i]);
        CHECK(sink.at(kPowerDownOrder[i]) == i);
        CHECK(to_string(kPowerDownOrder[i])[0] != '\0');
    }
}

// The bug this whole sequence exists to prevent: 0xB9 arrives at a part that has
// already lost its supply, the command does nothing, and an external flash draws
// its full standby current for the whole night the device spends in a flight bag
// looking switched off.
TEST_CASE("power down: the external flash is told to sleep while it still has a rail") {
    RecordingSink sink;
    power_down(sink);
    CHECK(sink.at(PowerDownStep::ExternalFlashDeepPowerDown) <
          sink.at(PowerDownStep::PeripheralRailOff));
    // And over the lines the command travels on, which is why they are released
    // after it and not with the rest of the pins.
    CHECK(sink.at(PowerDownStep::ExternalFlashDeepPowerDown) <
          sink.at(PowerDownStep::ExternalFlashLinesReleased));
}

TEST_CASE("power down: the radio is asleep before anything touches its reset line") {
    RecordingSink sink;
    power_down(sink);
    CHECK(sink.at(PowerDownStep::RadioSleep) < sink.at(PowerDownStep::RadioResetAsserted));
    CHECK(sink.at(PowerDownStep::RadioSleep) < sink.at(PowerDownStep::DrivenPinsReleased));
}

TEST_CASE("power down: the GNSS is switched off, and before its supply goes") {
    RecordingSink sink;
    power_down(sink);
    // The receiver with a fix is tens of milliamps: the one part that decides
    // whether an 850 mAh pack survives a night.
    REQUIRE(sink.at(PowerDownStep::GnssBackupOff) >= 0);
    CHECK(sink.at(PowerDownStep::GnssBackupOff) < sink.at(PowerDownStep::GnssResetAsserted));
    CHECK(sink.at(PowerDownStep::GnssResetAsserted) < sink.at(PowerDownStep::PeripheralRailOff));
}

TEST_CASE("power down: the rails go last, and the pins are released after them") {
    RecordingSink sink;
    power_down(sink);
    const int rail = sink.at(PowerDownStep::PeripheralRailOff);
    const int aux = sink.at(PowerDownStep::AuxRailOff);
    for (const PowerDownStep step :
         {PowerDownStep::RadioSleep, PowerDownStep::ExternalFlashDeepPowerDown,
          PowerDownStep::ExternalFlashLinesReleased, PowerDownStep::GnssBackupOff,
          PowerDownStep::GnssResetAsserted, PowerDownStep::RadioResetAsserted}) {
        CHECK(sink.at(step) < rail);
        CHECK(sink.at(step) < aux);
    }
    // A pin driven high into a part with no supply powers it through its own
    // protection diode, so nothing is released until both rails are down.
    CHECK(rail < sink.at(PowerDownStep::DrivenPinsReleased));
    CHECK(aux < sink.at(PowerDownStep::DrivenPinsReleased));
    // And the wake pin is level-sensed: armed before the rails, it wakes the
    // device the instant they drop.
    CHECK(sink.at(PowerDownStep::WakePinArmed) == kPowerDownStepCount - 1);
}

// Which reset the device came out of. Seven causes, one register, and the nRF52
// latches them: after a watchdog bite that was followed by a soft reset, both
// bits are set and only one of the two is the diagnosis.

TEST_CASE("reset: a fault cause outranks the benign one that came with it") {
    CHECK(classify(ResetCause::Watchdog | ResetCause::Software) == ResetReason::Watchdog);
    CHECK(classify(ResetCause::Lockup | ResetCause::Pin) == ResetReason::Lockup);
    CHECK(classify(ResetCause::Brownout | ResetCause::PowerOn) == ResetReason::Brownout);
    CHECK(classify(ResetCause::Watchdog | ResetCause::Lockup) == ResetReason::Watchdog);
}

TEST_CASE("reset: every cause the silicon can raise has one name") {
    CHECK(classify(ResetCause::None) == ResetReason::Unknown);
    CHECK(classify(ResetCause::PowerOn) == ResetReason::PowerOn);
    CHECK(classify(ResetCause::Pin) == ResetReason::Pin);
    CHECK(classify(ResetCause::Brownout) == ResetReason::Brownout);
    CHECK(classify(ResetCause::Software) == ResetReason::Software);
    CHECK(classify(ResetCause::Watchdog) == ResetReason::Watchdog);
    CHECK(classify(ResetCause::Lockup) == ResetReason::Lockup);
    CHECK(classify(ResetCause::LowPowerWake) == ResetReason::LowPowerWake);
    CHECK(classify(ResetCause::UsbVbus) == ResetReason::ChargerWake);
    CHECK(classify(ResetCause::Debug) == ResetReason::Debug);

    // A reason with no name reaches the panel as a blank, which is the one
    // outcome a self-test page must never produce.
    for (uint8_t i = 0; i <= static_cast<uint8_t>(ResetReason::Debug); i++) {
        const char* name = to_string(static_cast<ResetReason>(i));
        CHECK(name[0] != '\0');
    }
}

// D. The wake cause decides the boot path. On this board a VBUS event is a wake
// source, so plugging a charger into a device that was switched off brings the
// SoC up - and a device that boots in a flight bag because someone plugged it in
// arrives flat. These are the four bits that have to agree before a boot is
// refused, and the many ways they can fail to.

// The one the item exists for.
TEST_CASE("wake: a charger plugged into a sleeping device does not switch it on") {
    const ResetCause charger = ResetCause::LowPowerWake | ResetCause::UsbVbus;
    CHECK(boot_path(charger, /*button_down=*/false) == BootPath::SleepAgain);
    // And the page a bench eye reads names it, rather than calling it a wake.
    CHECK(classify(charger) == ResetReason::ChargerWake);
    CHECK(std::string(to_string(ResetReason::ChargerWake)) == "CHARGER");
}

TEST_CASE("wake: a pilot holding the button while plugging in gets the device") {
    const ResetCause charger = ResetCause::LowPowerWake | ResetCause::UsbVbus;
    CHECK(boot_path(charger, /*button_down=*/true) == BootPath::Run);
}

// The failure that would be catastrophic and silent: a unit that refuses to boot
// the first time a cell is connected. On the nRF52840 RESETREAS is all-zero after
// a power-on or brown-out reset, so the wake bit is what separates the two, and
// the rule needs BOTH bits rather than either.
TEST_CASE("wake: nothing that could be a first power-on ever refuses a boot") {
    CHECK(boot_path(ResetCause::PowerOn, false) == BootPath::Run);
    CHECK(boot_path(ResetCause::None, false) == BootPath::Run);
    CHECK(boot_path(ResetCause::Brownout, false) == BootPath::Run);
    // VBUS with no wake bit: not a wake at all, whatever raised it.
    CHECK(boot_path(ResetCause::UsbVbus, false) == BootPath::Run);
    CHECK(boot_path(ResetCause::PowerOn | ResetCause::UsbVbus, false) == BootPath::Run);
}

TEST_CASE("wake: the button and the reset pin are both a request for a device") {
    // The ordinary way out of SYSTEM OFF: a press on the wake pin.
    CHECK(boot_path(ResetCause::LowPowerWake, false) == BootPath::Run);
    // A deliberate reset while the cable happens to be in.
    CHECK(boot_path(ResetCause::LowPowerWake | ResetCause::UsbVbus | ResetCause::Pin, false) ==
          BootPath::Run);
    CHECK(boot_path(ResetCause::Pin, false) == BootPath::Run);
    // And a fault is never answered by going back to sleep, whatever else is set.
    CHECK(boot_path(ResetCause::Watchdog | ResetCause::UsbVbus, false) == BootPath::Run);
}

TEST_CASE("wake: a charger wake is named without hiding a fault that came with it") {
    // The diagnosis a pilot needs is still the fault: the charger only names the
    // boot when nothing worse did.
    CHECK(classify(ResetCause::Watchdog | ResetCause::UsbVbus) == ResetReason::Watchdog);
    CHECK(classify(ResetCause::Pin | ResetCause::UsbVbus) == ResetReason::Pin);
    CHECK(std::string(to_string(BootPath::SleepAgain)) == "SLEEP AGAIN");
    CHECK(std::string(to_string(BootPath::Run)) == "RUN");
}

// E1. Below the warning level nothing durable is written except the record that
// must survive. The rule is one function over the levels the monitor above
// already publishes, so "stop writing" happens at the same voltage the panel says
// LOW at, and there is no second threshold to keep in step with the first.

TEST_CASE("write gate: a settings write is refused below the warning, the log record is not") {
    CHECK(may_write(PowerLevel::Normal, false, DurableWrite::Settings));
    CHECK_FALSE(may_write(PowerLevel::Low, false, DurableWrite::Settings));
    CHECK_FALSE(may_write(PowerLevel::Cutoff, false, DurableWrite::Settings));

    // The one write whose value is highest exactly when the cell is lowest. A
    // landing out with no log is the flight a pilot needed the log for.
    for (const PowerLevel level :
         {PowerLevel::Unknown, PowerLevel::Normal, PowerLevel::Low, PowerLevel::Cutoff})
        CHECK(may_write(level, /*supply_warned=*/true, DurableWrite::FlightRecord));
}

// A gauge that never read a believable millivolt is not a reason to make the
// settings page stop working: an unpopulated or unconnected divider is Unknown,
// which the same floor keeps out of the cutoff decision.
TEST_CASE("write gate: a device with no believable reading still writes its settings") {
    CHECK(may_write(PowerLevel::Unknown, false, DurableWrite::Settings));

    CutoffMonitor floating;
    for (int i = 0; i < 20; i++) floating.apply(sample(0));
    REQUIRE(floating.level() == PowerLevel::Unknown);
    CHECK(floating.may_write(DurableWrite::Settings));
}

TEST_CASE("write gate: the monitor answers it from the samples it already has") {
    CutoffMonitor monitor;
    CHECK(monitor.may_write(DurableWrite::Settings));

    for (int i = 0; i < kConsecutiveSamples; i++) monitor.apply(sample(3400));
    REQUIRE(monitor.level() == PowerLevel::Low);
    CHECK_FALSE(monitor.may_write(DurableWrite::Settings));
    CHECK(monitor.may_write(DurableWrite::FlightRecord));

    // A charger arriving is what makes it writable again: the terminal is held
    // above the cell and nothing may be read into it, so the monitor is Normal.
    for (int i = 0; i < 2; i++) monitor.apply(sample(3400, /*external_power=*/true));
    CHECK(monitor.level() == PowerLevel::Normal);
    CHECK(monitor.may_write(DurableWrite::Settings));
}

// POFCON. The comparator watches the SoC's own rail, which is at or below the
// cell, so by the time it fires the divider's opinion is no longer the question.
TEST_CASE("write gate: a fired power-failure comparator outranks a healthy reading") {
    CutoffMonitor monitor;
    for (int i = 0; i < 4; i++) monitor.apply(sample(4000));
    REQUIRE(monitor.level() == PowerLevel::Normal);
    REQUIRE(monitor.may_write(DurableWrite::Settings));

    monitor.on_supply_warning();
    CHECK(monitor.supply_warned());
    CHECK(monitor.supply_warnings() == 1);
    CHECK_FALSE(monitor.may_write(DurableWrite::Settings));
    CHECK(monitor.may_write(DurableWrite::FlightRecord));

    // Latching: a rail that came back up does not make it un-happen, and a
    // charger does not either.
    for (int i = 0; i < 10; i++) monitor.apply(sample(4100, /*external_power=*/true));
    CHECK_FALSE(monitor.may_write(DurableWrite::Settings));
}

// It refuses writes; it does not power the device off. POFCON warns about VDD,
// well under any healthy cell, so there is no orderly shutdown left to run: the
// panel park alone is kParkMs and the brownout reset is milliseconds away. The
// voltage rule keeps the shutdown, where three consecutive samples are the
// evidence.
TEST_CASE("write gate: the comparator stops writes without switching the device off") {
    CutoffMonitor monitor;
    for (int i = 0; i < 4; i++) monitor.apply(sample(3900));
    monitor.on_supply_warning();
    CHECK_FALSE(monitor.cutoff());
    CHECK(monitor.level() == PowerLevel::Normal);

    monitor.on_supply_warning();
    CHECK(monitor.supply_warnings() == 2);
    CHECK_FALSE(monitor.cutoff());
}
