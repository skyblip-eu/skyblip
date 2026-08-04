#ifndef SKYBLIP_HAL_CAPABILITIES_H
#define SKYBLIP_HAL_CAPABILITIES_H

#include <cstdint>

namespace skyblip::hal {

enum class Capability : uint32_t {
    None = 0,
    Rf = 1u << 0,
    Gnss = 1u << 1,
    Display = 1u << 2,
    Baro = 1u << 3,
    Buzzer = 1u << 4,
    Vibro = 1u << 5,
    Link = 1u << 6,
    Storage = 1u << 7,
    Dfu = 1u << 8,
    Button = 1u << 9,
    Battery = 1u << 10,
    // The SoC's own die sensor (hal/die_temperature.h). Next free shift, and the
    // existing eleven are not renumbered: nothing persists this bitset, but
    // products/skyblip_go/product.h asserts it row by row on the self-test page,
    // and a shift that moved would move a row with it.
    DieTemperature = 1u << 11,
    // The haptic is an I2C waveform driver (a DRV2605), not a motor on a pin.
    // Vibro says a pulse can be made at all; this says by what, because the two
    // need different bring-up and a board wired for one and fitted with the other
    // reports PASS and stays silent. hal::Inventory carries the same fact for the
    // bench; this is the half the code branches on.
    HapticDriver = 1u << 12,
    // A status lamp (hal/indicator.h). E-paper holds its last image with the
    // rails down, so on a board without this the device has no way at all to say
    // it is alive. It is granted from the devicetree, not from the board being a
    // Plus: absent, the same table in core/indication runs and lights nothing.
    Indicator = 1u << 13,
};

using Capabilities = Capability;

constexpr Capabilities operator|(Capabilities a, Capabilities b) {
    return static_cast<Capabilities>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr Capabilities& operator|=(Capabilities& a, Capabilities b) {
    a = a | b;
    return a;
}

constexpr bool has(Capabilities set, Capabilities wanted) {
    return (static_cast<uint32_t>(set) & static_cast<uint32_t>(wanted)) ==
           static_cast<uint32_t>(wanted);
}

constexpr Capabilities missing(Capabilities set, Capabilities required) {
    return static_cast<Capabilities>(static_cast<uint32_t>(required) & ~static_cast<uint32_t>(set));
}

}  // namespace skyblip::hal

#endif
