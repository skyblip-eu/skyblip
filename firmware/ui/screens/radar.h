#ifndef SKYBLIP_UI_SCREENS_RADAR_H
#define SKYBLIP_UI_SCREENS_RADAR_H

#include <cstdint>

#include "ui/framebuffer.h"

namespace skyblip::ui {

struct RadarTarget {
    int32_t north_m;
    int32_t east_m;
    int32_t up_m;
    uint8_t alarm_level;
};

struct RadarSnapshot {
    bool have_fix{false};
    int32_t range_m{10000};
    int n_targets{0};
    const RadarTarget* targets{nullptr};
    uint8_t max_alarm{0};
    bool coverage{false};
};

void draw_radar(Framebuffer& fb, const RadarSnapshot& snap);

}

#endif
