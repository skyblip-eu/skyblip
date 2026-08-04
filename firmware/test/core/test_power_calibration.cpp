// H. The gauge is uncalibrated per unit.
//
// The divider ratio is a devicetree fact and Zephyr's voltage-divider driver
// owns the conversion. What neither can know is this unit: two 1% resistors put
// the ratio out by up to 2%, which at a full cell is 84 mV, and the SAADC's own
// gain and reference error adds to it. These cases exist to show what that costs
// in the only unit a pilot reads - percentage points - and to pin the arithmetic
// that corrects it.
#include "core/power/battery.h"
#include "core/power/cutoff.h"
#include "core/settings/settings.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::power;

// The reason the field exists, in one number. 40 mV is well inside what two 1%
// resistors and an ADC can be out by, and in the flat middle of a Li-ion curve
// it is twelve percentage points of state of charge - the difference between a
// pilot flying another hour and a pilot landing.
TEST_CASE("battery: tens of millivolts are tens of percent in the flat middle") {
    CHECK(int(percent_from_mv(3800, false)) == 55);
    CHECK(int(percent_from_mv(3760, false)) == 43);
    CHECK(int(percent_from_mv(3840, false)) == 63);
}

TEST_CASE("battery: the trim is a signed millivolt offset, and zero is the identity") {
    // An uncalibrated unit reads exactly as it did before the field existed.
    for (uint16_t raw = 3000; raw <= 4300; raw = static_cast<uint16_t>(raw + 100))
        CHECK(calibrated_mv(raw, 0) == raw);

    CHECK(calibrated_mv(3800, -40) == 3760);
    CHECK(calibrated_mv(3800, 40) == 3840);
    CHECK(calibrated_mv(3800, kCalibrationLimitMv) == 3800u + kCalibrationLimitMv);
    CHECK(calibrated_mv(3800, -kCalibrationLimitMv) == 3800u - kCalibrationLimitMv);
}

// The bound belongs to the settings boundary (core/settings validates it on the
// blob and on the JSON alike), so nothing here re-checks it. What is kept here
// is the arithmetic: a reading is an unsigned 16-bit millivolt figure and must
// stay one whatever it is handed, because a wrap would turn a flat cell into a
// full one on the way past a cutoff.
TEST_CASE("battery: a trim can never push a reading out of the range a reading has") {
    CHECK(calibrated_mv(0, -32768) == 0);
    CHECK(calibrated_mv(10, -300) == 0);
    CHECK(calibrated_mv(65535, 32767) == 65535);
    CHECK(calibrated_mv(65500, 300) == 65535);
}

// One trim, applied once, to the sample both readers see. The cutoff monitor and
// the gauge are separate objects fed from the same queue: if only one of them
// were trimmed, a calibrated unit would cut off at a voltage its own gauge never
// showed - the calibration causing the failure it exists to prevent.
TEST_CASE("battery: the trimmed sample is the same sample, charger state and all") {
    // Nothing but the millivolts moves: the charger flag decides which curve the
    // gauge reads and whether the cutoff monitor may act at all, and a trim has
    // no opinion about either.
    messages::BatterySample on_the_cable{};
    on_the_cable.millivolts = 4000;
    on_the_cable.external_power = true;
    CHECK(calibrated(on_the_cable, -60).external_power);
    CHECK(calibrated(on_the_cable, -60).millivolts == 3940);

    // The boundary that matters: this unit reads 60 mV high, so a cell the raw
    // sample puts above the low-battery warning is really below it, and both
    // readers must agree on which side of the line it is.
    messages::BatterySample raw{};
    raw.millivolts = 3540;
    const messages::BatterySample trimmed = calibrated(raw, -60);
    CHECK(raw.millivolts > kLowWarnMv);
    CHECK(trimmed.millivolts < kLowWarnMv);

    Gauge gauge;
    CutoffMonitor monitor;
    Gauge untrimmed_gauge;
    CutoffMonitor untrimmed_monitor;
    for (int i = 0; i < kConsecutiveSamples + 1; i++) {
        gauge.apply(trimmed);
        monitor.apply(trimmed);
        untrimmed_gauge.apply(raw);
        untrimmed_monitor.apply(raw);
    }
    CHECK(gauge.state().millivolts == 3480);
    CHECK(monitor.warned());
    // The same unit, uncalibrated, says the cell is fine.
    CHECK(untrimmed_gauge.state().millivolts == 3540);
    CHECK_FALSE(untrimmed_monitor.warned());
}

// The whole path, end to end, in the terms the line uses: a bench supply held at
// a known voltage, a unit that reads high, one number written into settings.
TEST_CASE("battery: a unit that reads high is corrected by one number from the line") {
    // The bench supply is at 3.80 V. This unit's divider and ADC report 3.85 V.
    constexpr uint16_t kBenchMv = 3800;
    constexpr uint16_t kThisUnitReadsMv = 3850;

    settings::Settings s = settings::defaults(1);
    const char* patch = "{\"battery_offset_mv\":-50}";
    REQUIRE(settings::apply_json(s, patch, 25) == Status::Ok);

    messages::BatterySample sample{};
    sample.millivolts = kThisUnitReadsMv;

    Gauge uncalibrated;
    Gauge trimmed;
    for (int i = 0; i < 3; i++) {
        uncalibrated.apply(sample);
        trimmed.apply(calibrated(sample, s.battery_offset_mv));
    }
    CHECK(uncalibrated.state().millivolts == kThisUnitReadsMv);
    CHECK(trimmed.state().millivolts == kBenchMv);
    CHECK(trimmed.state().percent == percent_from_mv(kBenchMv, false));
    // Ten percentage points of gauge error, from a divider inside tolerance.
    CHECK(int(uncalibrated.state().percent) == 65);
    CHECK(int(trimmed.state().percent) == 55);
}
