#ifndef SKYBLIP_UI_SCREENS_RADAR_H
#define SKYBLIP_UI_SCREENS_RADAR_H

#include <cstdint>

#include "ui/framebuffer.h"

namespace skyblip::ui {

constexpr int32_t kMetresPerNm = 1852;
constexpr int32_t kDefaultRangeNm = 4;

struct RadarTarget {
    int32_t north_m;
    int32_t east_m;
    int32_t up_m;
    uint8_t alarm_level;
};

struct RadarSnapshot {
    bool have_fix{false};
    int32_t range_nm{kDefaultRangeNm};
    uint16_t track_deg{0};
    uint8_t sats{0};
    int n_targets{0};
    const RadarTarget* targets{nullptr};
    uint8_t max_alarm{0};
};

void draw_radar(Framebuffer& fb, const RadarSnapshot& snap);

}

#endif
