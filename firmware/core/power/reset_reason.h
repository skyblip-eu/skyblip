// core/power/reset_reason.h: why the device came up, and which cause wins when
// the silicon reports several at once.
#ifndef SKYBLIP_CORE_POWER_RESET_REASON_H
#define SKYBLIP_CORE_POWER_RESET_REASON_H

#include <cstdint>

namespace skyblip::power {

// The causes a reset-cause register can raise, as our own bits. The platform
// adapter translates whatever the silicon calls them into this set, which is
// what keeps the priority rule below testable without a framework header.
enum class ResetCause : uint32_t {
    None = 0,
    PowerOn = 1u << 0,
    Pin = 1u << 1,
    Brownout = 1u << 2,
    Software = 1u << 3,
    Watchdog = 1u << 4,
    Lockup = 1u << 5,
    LowPowerWake = 1u << 6,
    Debug = 1u << 7,
};

constexpr ResetCause operator|(ResetCause a, ResetCause b) {
    return static_cast<ResetCause>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr ResetCause& operator|=(ResetCause& a, ResetCause b) {
    a = a | b;
    return a;
}

constexpr bool has_cause(ResetCause set, ResetCause wanted) {
    return (static_cast<uint32_t>(set) & static_cast<uint32_t>(wanted)) != 0;
}

enum class ResetReason : uint8_t {
    Unknown,
    PowerOn,
    Pin,
    Brownout,
    Software,
    Watchdog,
    Lockup,
    LowPowerWake,
    Debug,
};

// One reason out of a set of bits.
//
// INFO: hk 02aug26 nRF52 RESETREAS latches and is cleared only by writing ones
// back, so a soft reset that followed a watchdog bite comes up with both bits
// set (nRF52840 PS v1.8 §5.3.3). The diagnosis a pilot needs is the fault, so a
// fault cause outranks a benign one whatever else is also set.
ResetReason classify(ResetCause causes);

const char* to_string(ResetReason reason);

}  // namespace skyblip::power

#endif
