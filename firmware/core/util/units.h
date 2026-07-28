#ifndef SKYBLIP_CORE_UTIL_UNITS_H
#define SKYBLIP_CORE_UTIL_UNITS_H

#include <cstdint>

namespace skyblip {

struct Metres {
    int32_t v{0};
    constexpr Metres() = default;
    constexpr explicit Metres(int32_t m) : v(m) {}
    constexpr bool operator==(Metres o) const { return v == o.v; }
};

struct Feet {
    int32_t v{0};
    constexpr Feet() = default;
    constexpr explicit Feet(int32_t f) : v(f) {}
};

constexpr Metres to_metres(Feet f) { return Metres((f.v * 2497 + 4096) >> 13); }
constexpr Feet to_feet(Metres m) { return Feet((m.v * 3360 + 512) >> 10); }

struct QuarterMetresPerSec {
    uint16_t v{0};
    constexpr QuarterMetresPerSec() = default;
    constexpr explicit QuarterMetresPerSec(uint16_t q) : v(q) {}
};
struct MetresPerSec {
    int32_t v{0};
    constexpr MetresPerSec() = default;
    constexpr explicit MetresPerSec(int32_t m) : v(m) {}
};
constexpr MetresPerSec to_mps(QuarterMetresPerSec q) { return MetresPerSec(q.v / 4); }

struct EighthMetresPerSec {
    int16_t v{0};
    constexpr EighthMetresPerSec() = default;
    constexpr explicit EighthMetresPerSec(int16_t e) : v(e) {}
};

struct Cordic9 {
    uint16_t v{0};
    constexpr Cordic9() = default;
    constexpr explicit Cordic9(uint16_t c) : v(static_cast<uint16_t>(c & 0x1FF)) {}
};
struct Degrees {
    uint16_t v{0};
    constexpr Degrees() = default;
    constexpr explicit Degrees(uint16_t d) : v(d) {}
};
constexpr Degrees to_degrees(Cordic9 c) {
    return Degrees(static_cast<uint16_t>((static_cast<uint32_t>(c.v) * 45 + 32) >> 6) % 360);
}

struct MilliVolts {
    int16_t v{0};
    constexpr MilliVolts() = default;
    constexpr explicit MilliVolts(int16_t mv) : v(mv) {}
};

}

#endif
