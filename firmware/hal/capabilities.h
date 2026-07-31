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
