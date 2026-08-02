#include "core/flight/turn.h"

namespace skyblip::flight {

namespace {
constexpr int32_t kHalfTurnC9 = kTrackC9Turn / 2;
// 512 cordic9 units to 360 degrees: 45/64 of a degree each.
constexpr int32_t kDegPerTurnNum = 45;
constexpr int32_t kDegPerTurnDen = 64;
}  // namespace

int16_t track_delta_c9(uint16_t track_c9, uint16_t ref_track_c9) {
    const int32_t diff =
        ((static_cast<int32_t>(track_c9 & kTrackC9Mask) -
          static_cast<int32_t>(ref_track_c9 & kTrackC9Mask) + kTrackC9Turn + kHalfTurnC9) %
         kTrackC9Turn) -
        kHalfTurnC9;
    return static_cast<int16_t>(diff);
}

int16_t turn_rate_dps(uint16_t track_c9, uint16_t ref_track_c9, uint32_t dt_ms) {
    if (dt_ms == 0) return 0;
    const int32_t diff = track_delta_c9(track_c9, ref_track_c9);
    return static_cast<int16_t>((diff * kDegPerTurnNum * 1000) /
                                (kDegPerTurnDen * static_cast<int32_t>(dt_ms)));
}

}  // namespace skyblip::flight
