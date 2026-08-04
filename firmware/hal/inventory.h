// hal/inventory.h: what the board found when it asked its own hardware, as
// opposed to what it was compiled expecting to find.
//
// hal/capabilities.h carries the verdicts - present, absent, required. This
// carries the identities behind them, because LilyGO ships more than one part
// against the same footprint and the self-test page is the only place a bench
// can see which one it is holding: which of the two BME280 addresses answered,
// which e-paper lot the glass is from, whether the haptic is a waveform driver
// or a motor on a pin. Names and addresses only: nothing here decides anything.
#ifndef SKYBLIP_HAL_INVENTORY_H
#define SKYBLIP_HAL_INVENTORY_H

#include <cstdint>

namespace skyblip::hal {

// The haptic actually established at bring-up. Absent is a capability
// (Capability::Vibro); this says which kind of hardware the pulse goes to, so a
// board that is wired for one and fitted with the other is visible rather than
// silent.
enum class HapticKind : uint8_t { None, PinMotor, WaveformDriver };

struct Inventory {
    // The addresses that answered a bus scan, in ascending order. Everything
    // fitted on this board's I2C bus is in here, including the parts nothing
    // drives, because "0x51 answered" is the whole evidence for an RTC.
    static constexpr int kMaxI2cAddresses = 8;
    uint8_t i2c_addresses[kMaxI2cAddresses]{};
    uint8_t i2c_count{0};

    // 0 when no barometer answered.
    uint8_t baro_address{0};
    // parts::panel_name() of the identified panel, or its unknown label. Never
    // null: the page prints it.
    const char* panel{"?"};
    HapticKind haptic{HapticKind::None};

    bool has_i2c_address(uint8_t address) const {
        for (uint8_t i = 0; i < i2c_count; i++)
            if (i2c_addresses[i] == address) return true;
        return false;
    }

    // Ascending, no duplicates, and it drops what it cannot hold rather than
    // running off the end of the array.
    bool add_i2c_address(uint8_t address) {
        if (i2c_count >= kMaxI2cAddresses || has_i2c_address(address)) return false;
        uint8_t at = i2c_count;
        while (at > 0 && i2c_addresses[at - 1] > address) {
            i2c_addresses[at] = i2c_addresses[at - 1];
            at--;
        }
        i2c_addresses[at] = address;
        i2c_count++;
        return true;
    }
};

}  // namespace skyblip::hal

#endif
