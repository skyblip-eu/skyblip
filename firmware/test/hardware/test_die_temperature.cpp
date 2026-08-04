// The die temperature port, from the side that has no sensor.
//
// On this platform there is no silicon to read, and that is the case worth
// pinning: absent hardware is a capability, so the port answers "no reading" and
// leaves the caller's value exactly as it found it. Everything downstream depends
// on that one property - a status reply omits the key rather than publishing a
// zero, and a zero would read as 0.0 C, which is a perfectly plausible morning
// in a hangar and completely wrong.
//
// The nRF52840 side (hardware/platform/zephyr/die_temperature.h, Zephyr's
// temp_nrf5 driver on "nordic,nrf-temp") cannot be built on the host and is not
// reached from here. It needs CONFIG_SENSOR and CONFIG_TEMP_NRF5, and a bench
// reading beside a thermocouple before any number it produces is quoted.
#include <cstdint>

#include "doctest/doctest.h"
#include "hal/die_temperature.h"

using namespace skyblip;

TEST_CASE("die temperature: an absent sensor answers nothing and spoils nothing") {
    hal::DieTemperature absent;

    int16_t decicelsius = 1234;
    CHECK_FALSE(absent.read(decicelsius));
    // Untouched, not zeroed: the caller's own initial value survives, so a
    // reading nobody took can never be mistaken for one that was.
    CHECK(decicelsius == 1234);

    int16_t fresh = 0;
    CHECK_FALSE(absent.read(fresh));
    CHECK(fresh == 0);
}
