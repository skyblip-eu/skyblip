// The haptic driver, against its model.
//
// The bug this part exists to fix: pins.h named P0.08 "DRV haptic / vibration
// motor enable" and the annunciator drove it as a plain GPIO with a timer, so
// every claim in the test suite about the alarm escalating to vibration was a
// claim about a pin. On a T-Echo Plus that pin is a DRV2605's enable: raising it
// brings a waveform driver out of hardware standby and moves nothing at all. The
// first test below is that fact, and it fails against the old annunciator.
#include "doctest/doctest.h"
#include "hardware/parts/drv2605/drv2605.h"
#include "hardware/parts/drv2605/model.h"

using namespace skyblip;

namespace {

parts::Drv2605 make(models::Drv2605& chip) { return parts::Drv2605(chip, chip, chip.enable_pin); }

}  // namespace

TEST_CASE("drv2605: the enable pin on its own produces no pulse") {
    models::Drv2605 chip;
    parts::Drv2605 haptic = make(chip);

    // Exactly what the annunciator used to do: drive the pin, wait, drop it.
    haptic.power_up();
    CHECK(chip.enabled);
    CHECK(chip.enable_raises == 1);
    CHECK_FALSE(chip.moving());
    CHECK(chip.drive() == 0);
}

TEST_CASE("drv2605: bring-up identifies the part and leaves it configured and asleep") {
    models::Drv2605 chip;
    parts::Drv2605 haptic = make(chip);

    REQUIRE(haptic.begin() == Status::Ok);
    CHECK(haptic.ready());
    // STATUS[7:5] = 7 is the DRV2605L the model presents.
    CHECK(int(haptic.device_id()) == 7);

    // The enable line is up before anything on the bus is believed.
    CHECK(chip.enabled);
    // An ERM in open loop: the fitted motor is in no datasheet we have, so
    // closed loop has no rated voltage or back-EMF it could trust.
    CHECK(chip.erm_selected());
    CHECK(chip.open_loop());
    CHECK(int(chip.library()) == 1);
    // Configured, and drawing nothing: a driver stage idling is milliamps on a
    // device that counts microamps in a flight bag.
    CHECK(chip.standby());
    CHECK_FALSE(chip.moving());
}

TEST_CASE("drv2605: a part that does not answer its address is absent, not broken") {
    models::Drv2605 chip;
    chip.answers = false;
    parts::Drv2605 haptic = make(chip);

    CHECK(haptic.begin() == Status::Down);
    CHECK_FALSE(haptic.ready());

    // And it stays silent: a driver that pretends is worse than one that admits.
    haptic.start();
    CHECK_FALSE(chip.moving());
}

TEST_CASE("drv2605: something else at 0x5A is not a haptic driver") {
    models::Drv2605 chip;
    chip.registers[0x00] = 0x00;  // device id 0: not any DRV260x
    parts::Drv2605 haptic = make(chip);

    CHECK(haptic.begin() == Status::Unsupported);
    CHECK_FALSE(haptic.ready());
    haptic.start();
    CHECK_FALSE(chip.moving());
}

TEST_CASE("drv2605: a pulse is a drive value that holds, and stops when it is told") {
    models::Drv2605 chip;
    parts::Drv2605 haptic = make(chip);
    REQUIRE(haptic.begin() == Status::Ok);

    haptic.start();
    CHECK(chip.moving());
    CHECK_FALSE(chip.standby());
    // Real-time playback: the drive value is a register that holds, so the pulse
    // is as long as the adapter's timer rather than as long as some entry in the
    // effect ROM happens to be.
    CHECK(int(chip.mode()) == 0x05);
    CHECK(int(chip.drive()) == 0x7F);

    // It holds. Nothing here is a one-shot that a second reader could miss.
    CHECK(chip.moving());

    haptic.stop();
    CHECK_FALSE(chip.moving());
    CHECK(chip.standby());
}

TEST_CASE("drv2605: a second start inside a pulse does not restack the bus traffic") {
    models::Drv2605 chip;
    parts::Drv2605 haptic = make(chip);
    REQUIRE(haptic.begin() == Status::Ok);

    haptic.start();
    const int writes = chip.writes;
    haptic.start();
    CHECK(chip.writes == writes);
    CHECK(chip.moving());
}

TEST_CASE("drv2605: parking clears the GO bit a bench session may have left set") {
    models::Drv2605 chip;
    parts::Drv2605 haptic = make(chip);
    REQUIRE(haptic.begin() == Status::Ok);

    // A ROM sequence left running by anything else: mode 0, GO set, waveform 1.
    chip.registers[0x01] = 0x00;
    chip.registers[0x04] = 0x01;
    chip.registers[0x0C] = 0x01;
    REQUIRE(chip.moving());

    haptic.park();
    CHECK_FALSE(chip.moving());
    CHECK(int(chip.registers[0x0C]) == 0);
    // The enable line is released rather than driven low, which is what the
    // reference does on the way down: no driven output into a collapsing rail.
    CHECK_FALSE(chip.enabled);
}
