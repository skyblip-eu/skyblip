// What is actually on this board's sensor bus.
//
// One loop, so nothing downstream has to guess. Before this, the self-test page
// could say "BARO" but not which of the two declared BME280 addresses answered
// (the devicetree declares both because our BOM and LilyGO's own README
// disagree), the haptic driver's presence was a special case nobody had written,
// and the RTC that project/2-DEVICES.md:73 says is fitted had no evidence behind
// it at all.
//
// A zero-length write is the probe: it addresses the part and stops, which is
// what Zephyr's own i2c shell scan does and what every Arduino
// beginTransmission/endTransmission pair the references use amounts to
// (SoftRF platform/nRF52.cpp:1128-1163 probes the RTC, the IMU and the DRV2605
// exactly this way).
#ifndef SKYBLIP_HARDWARE_BOARDS_T_ECHO_PLUS_I2C_SCAN_H
#define SKYBLIP_HARDWARE_BOARDS_T_ECHO_PLUS_I2C_SCAN_H

#include <cstdint>

#include "hal/inventory.h"
#include "hardware/io/io.h"

namespace skyblip::boards::t_echo_plus {

// 7-bit addresses. 0x00-0x07 and 0x78-0x7F are reserved by the I2C
// specification, so a part cannot live there and a probe of one is a transfer
// that means nothing.
constexpr uint8_t kI2cFirstAddress = 0x08;
constexpr uint8_t kI2cLastAddress = 0x77;

// Everything this board is documented to carry, so the page can name what it
// found rather than printing hex at a pilot. Sources: project/2-DEVICES.md's
// hardware matrix, and SoftRF platform/nRF52.h:136-151 for the same three parts.
constexpr uint8_t kHapticDriverAddress = 0x5A;  // DRV2605
constexpr uint8_t kImuAddress = 0x28;           // BHI260AP (Plus), deliberately undriven
constexpr uint8_t kImuAddressAlternate = 0x29;  // BHI260AP, address-select high
constexpr uint8_t kRtcAddress = 0x51;           // PCF8563, deliberately undriven

// The part the references only trust after a settling delay: "MPU9250 or
// ICM20948 start-up time for register R/W is 11-100 ms", SoftRF
// platform/nRF52.cpp:1145-1147, which waits 90 ms before probing an IMU at all.
// The rails on this board go up in board.c at PRE_KERNEL_1 and the scan runs
// after the whole driver model is initialised, which is milliseconds of clock
// setup, flash and USB later - so the delay is paid for by construction rather
// than by a sleep. The figure is here because that is an accident of boot order,
// and the day a scan moves earlier it stops being true.
constexpr uint32_t kImuSettlingMs = 90;

// Every address on the bus, in one pass. 112 probes at 100 kHz is about 20 ms of
// bus time, once, at boot.
inline hal::Inventory scan_i2c(io::I2c& bus) {
    hal::Inventory inventory{};
    for (uint8_t address = kI2cFirstAddress; address <= kI2cLastAddress; address++) {
        if (!bus.write(address, nullptr, 0)) continue;
        inventory.add_i2c_address(address);
    }
    return inventory;
}

}  // namespace skyblip::boards::t_echo_plus

#endif
