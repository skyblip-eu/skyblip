#include "core/flight/extrapolate.h"

#include "core/util/intmath.h"

namespace skyblip::flight {

namespace {

constexpr int64_t kTrigOne = 16384;
constexpr int kTrackC9ToAngle = 7;
constexpr int32_t kTrackC9Mask = 0x1FF;
constexpr int64_t kTurn16 = 65536;
constexpr int64_t kMilliDegreesPerTurn = 360000;
// The tree's one figure for the size of the earth, as core/protocol/nmea_out
// carries it: 1e-7 degree of latitude is 11132 micrometres.
constexpr int64_t kMicrometresPerE7 = 11132;
constexpr int64_t kSpeedQPerMps = 4;
constexpr int64_t kClimbE8PerMps = 8;
constexpr int64_t kMicrometresPerMetre = 1000000;
constexpr int64_t kMsPerS = 1000;
constexpr int64_t kE7PerTurn = 3600000000LL;
// Below this cosine a metre of easting is more than a degree of longitude and
// the scaling stops meaning anything. 88 degrees of latitude is 500 km further
// north than anything this device will fly over.
constexpr int64_t kMinLatCosine = 512;

int64_t div_round(int64_t num, int64_t den) {
    return num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den);
}

int64_t iabs64(int64_t v) { return v < 0 ? -v : v; }

int16_t lat_angle16(int32_t lat_1e7) {
    return static_cast<int16_t>((static_cast<int64_t>(lat_1e7) * kTurn16) / kE7PerTurn);
}

int64_t lat_cosine(int32_t lat_1e7) {
    const int64_t c = icos(lat_angle16(lat_1e7));
    return c < kMinLatCosine ? kMinLatCosine : c;
}

int16_t angle16_of(uint16_t track_c9) {
    return static_cast<int16_t>(
        static_cast<uint16_t>((track_c9 & kTrackC9Mask) << kTrackC9ToAngle));
}

// Three angle units meet in this file: degrees per second off the turn rate,
// cordic9 on the wire, and the 16-bit cordic the sine table is indexed by.
// This is the only conversion between the first and the last.
int32_t turn_angle16(int16_t turn_dps, int32_t dt_ms) {
    return static_cast<int32_t>(
        div_round(static_cast<int64_t>(turn_dps) * dt_ms * kTurn16, kMilliDegreesPerTurn));
}

Prediction passthrough(const messages::OwnState& own) {
    Prediction out{};
    out.lat_1e7 = own.lat_1e7;
    out.lon_1e7 = own.lon_1e7;
    out.alt_m = own.alt_m;
    out.alt_msl_m = own.alt_msl_m;
    out.track_c9 = static_cast<uint16_t>(own.track_c9 & kTrackC9Mask);
    out.valid = false;
    return out;
}

}  // namespace

Prediction extrapolate(const messages::OwnState& own, int32_t dt_ms) {
    Prediction out = passthrough(own);
    if (!own.fix_valid) return out;
    if (dt_ms > kMaxExtrapolationMs || dt_ms < -kMaxExtrapolationMs) return out;
    out.valid = true;
    if (dt_ms == 0) return out;

    const int32_t turn16 = turn_angle16(own.turn_dps, dt_ms);
    const int16_t heading =
        static_cast<int16_t>(static_cast<uint16_t>(angle16_of(own.track_c9) + turn16 / 2));

    const int64_t scale = kTrigOne * kMicrometresPerE7;
    const int64_t travel = static_cast<int64_t>(own.speed_q) * dt_ms * kMicrometresPerMetre /
                           (kSpeedQPerMps * kMsPerS);
    out.lat_1e7 = own.lat_1e7 + static_cast<int32_t>(div_round(travel * icos(heading), scale));
    const int64_t east = div_round(travel * isin(heading), scale);
    out.lon_1e7 =
        own.lon_1e7 + static_cast<int32_t>(div_round(east * kTrigOne, lat_cosine(own.lat_1e7)));

    if (own.climb_valid) {
        const int32_t rise = static_cast<int32_t>(
            div_round(static_cast<int64_t>(own.climb_e8) * dt_ms, kClimbE8PerMps * kMsPerS));
        out.alt_m = own.alt_m + rise;
        out.alt_msl_m = own.alt_msl_m + rise;
    }

    const uint16_t track16 =
        static_cast<uint16_t>(angle16_of(own.track_c9) + turn16 + (1 << (kTrackC9ToAngle - 1)));
    out.track_c9 = static_cast<uint16_t>((track16 >> kTrackC9ToAngle) & kTrackC9Mask);
    return out;
}

uint32_t prediction_residual_m(const Prediction& predicted, int32_t lat_1e7, int32_t lon_1e7,
                               int32_t alt_m) {
    const int64_t dlat = static_cast<int64_t>(lat_1e7) - predicted.lat_1e7;
    const int64_t dlon = static_cast<int64_t>(lon_1e7) - predicted.lon_1e7;
    const int64_t north = div_round(dlat * kMicrometresPerE7, kMicrometresPerMetre);
    const int64_t east = div_round(dlon * kMicrometresPerE7 * lat_cosine(predicted.lat_1e7),
                                   kMicrometresPerMetre * kTrigOne);
    const int64_t up = static_cast<int64_t>(alt_m) - predicted.alt_m;
    return static_cast<uint32_t>(iabs64(north) + iabs64(east) + iabs64(up));
}

}  // namespace skyblip::flight
