// What the board asks its own hardware at bring-up, and what it concludes.
//
// The T-Echo Plus is not one board: LilyGO fits either of two flash parts,
// either of two barometer addresses, one of at least five e-paper lots, and -
// this is the one that bit us - a DRV2605 haptic driver where pins.h claimed a
// vibration motor on a pin. Every case here is the board being handed a
// different unit off the same production line.
#include <string>

#include "core/bus/bus.h"
#include "doctest/doctest.h"
#include "hardware/boards/lilygo/t_echo_plus/board.h"
#include "hardware/platform/host/platform.h"

using namespace skyblip;

namespace {

using Board = boards::TEchoPlus<platform::host::Platform>;
using boards::t_echo_plus::kHapticDriverAddress;
using boards::t_echo_plus::kImuAddress;
using boards::t_echo_plus::kRtcAddress;

}  // namespace

TEST_CASE("board: one scan names everything on the sensor bus, driven or not") {
    platform::host::Platform platform;
    bus::Bus bus;
    Board board{platform, bus};

    const hal::Inventory& inventory = board.inventory();

    // The barometer, at the address that actually answered rather than at
    // whichever of the two the devicetree happened to declare first.
    CHECK(int(inventory.baro_address) == 0x76);
    CHECK(inventory.has_i2c_address(0x76));
    CHECK_FALSE(inventory.has_i2c_address(0x77));

    // The haptic driver, which is the whole reason the scan exists.
    CHECK(inventory.has_i2c_address(kHapticDriverAddress));

    // And the two parts nothing drives, because a scan that hid the soldered-on
    // parts would be a scan nobody could use: this is the entire evidence for the
    // RTC that project/2-DEVICES.md records as fitted.
    CHECK(inventory.has_i2c_address(kRtcAddress));
    CHECK(inventory.has_i2c_address(kImuAddress));

    // Ascending, so the page reads the same way twice.
    for (uint8_t i = 1; i < inventory.i2c_count; i++)
        CHECK(inventory.i2c_addresses[i - 1] < inventory.i2c_addresses[i]);
}

TEST_CASE("board: a haptic that answers the bus is a capability, and it is the part") {
    platform::host::Platform platform;
    bus::Bus bus;
    Board board{platform, bus};

    CHECK(hal::has(board.capabilities(), hal::Capability::Vibro));
    CHECK(board.inventory().haptic == hal::HapticKind::WaveformDriver);
    CHECK(board.haptic().ready());

    // The pulse reaches the chip's registers, not a pin: the annunciator role the
    // alarm service holds is wired to the part the board found.
    hal::Roles roles = board.roles();
    models::Drv2605& chip = platform.chips().haptic;
    CHECK_FALSE(chip.moving());

    roles.annunciator.vibrate(200);
    CHECK(chip.moving());

    // And it stops on its own deadline, which is the adapter's promise in
    // hal/annunciator.h. The board's poll is where the host's deadline is served.
    bus::State state{};
    board.poll(state, 100);
    CHECK(chip.moving());
    board.poll(state, 100 + 200);
    CHECK_FALSE(chip.moving());
}

TEST_CASE("board: a Plus with no haptic driver does not claim a vibration motor") {
    platform::host::Platform platform;
    // The unit is fitted for a haptic and the part does not answer: an empty pad,
    // a dead chip, a board that turned out to be a plain T-Echo.
    platform.chips().haptic.answers = false;
    bus::Bus bus;
    Board board{platform, bus};

    CHECK_FALSE(hal::has(board.capabilities(), hal::Capability::Vibro));
    CHECK(board.inventory().haptic == hal::HapticKind::None);
    CHECK_FALSE(board.inventory().has_i2c_address(kHapticDriverAddress));

    // The call site does not change and does not check: it asks for a pulse and
    // gets whatever this board can make, which here is nothing at all.
    hal::Roles roles = board.roles();
    roles.annunciator.vibrate(200);
    CHECK_FALSE(platform.chips().haptic.moving());
}

TEST_CASE("board: the panel it identified is on the inventory by name") {
    platform::host::Platform platform;
    platform.chips().epd.signature = parts::panels::kGdeh0154D67Syx2118;
    bus::Bus bus;
    Board board{platform, bus};

    CHECK(board.display().panel() == parts::Panel::Gdeh0154D67Syx2118);
    CHECK(std::string(board.inventory().panel) == "D67/2118");
}

TEST_CASE("board: a panel nobody has fingerprinted is named, not guessed at") {
    platform::host::Platform platform;
    bus::Bus bus;
    Board board{platform, bus};

    // The default: no fingerprint taken, which is what this board will report on
    // silicon until a Plus is read on a bench.
    CHECK(board.display().panel() == parts::Panel::Unknown);
    CHECK(std::string(board.inventory().panel) == "UNKNOWN");
    // And the display is still a capability. An identification that could refuse
    // to drive the glass would blank the one page that explains a dead device.
    CHECK(hal::has(board.capabilities(), hal::Capability::Display));
}

TEST_CASE("board: a buzzer pin held low withdraws the buzzer and keeps the haptic") {
    platform::host::Platform platform;
    platform.set_buzzer_pin_held_low(true);
    bus::Bus bus;
    Board board{platform, bus};

    CHECK_FALSE(hal::has(board.capabilities(), hal::Capability::Buzzer));
    // The haptic is a different part on a different bus and is unaffected...
    CHECK(hal::has(board.capabilities(), hal::Capability::Vibro));

    // ...and it is still reachable, because the annunciator is one role over two
    // parts: a dead buzzer pin must not take the pulse with it.
    hal::Roles roles = board.roles();
    roles.annunciator.vibrate(200);
    CHECK(platform.chips().haptic.moving());
}

TEST_CASE("board: a barometer at LilyGO's address is the one reported") {
    platform::host::Platform platform;
    bus::Bus bus;
    // Our BOM says 0x76 and LilyGO's own README says 0x77. This is the unit that
    // came the other way.
    platform.i2c_bus().answer(0x76, false);
    platform.i2c_bus().answer(0x77, true);
    Board board{platform, bus};

    CHECK(int(board.inventory().baro_address) == 0x77);
}

TEST_CASE("board: the status lamp is a devicetree fact, and a board without one says so") {
    // The one part on this board that cannot be probed: an LED answers nothing.
    // So presence is the devicetree node existing and its GPIO port being ready,
    // which is the platform's question, and the board only carries the verdict
    // through. Asserted here because a board that dropped the bit would produce a
    // device with a lamp it never lights and no way to tell.
    platform::host::Platform fitted;
    bus::Bus bus;
    Board with_a_lamp{fitted, bus};
    CHECK(hal::has(with_a_lamp.capabilities(), hal::Capability::Indicator));

    constexpr hal::Capabilities kNoLamp = static_cast<hal::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(hal::Capability::Indicator));
    platform::host::Platform bare{kNoLamp};
    bus::Bus bare_bus;
    Board without{bare, bare_bus};
    CHECK_FALSE(hal::has(without.capabilities(), hal::Capability::Indicator));
    // And it is still a flyable board: the lamp is not required for anything.
    CHECK(hal::has(without.capabilities(), hal::Capability::Rf | hal::Capability::Gnss));
}
