// ui/screens/sixpack.h: the classic six-pack instrument panel, drawn from the
// GNSS-derived own-ship state: airspeed, attitude, altimeter over turn
// coordinator, heading, vertical speed.
#ifndef SKYBLIP_UI_SCREENS_SIXPACK_H
#define SKYBLIP_UI_SCREENS_SIXPACK_H

#include <cstdint>

#include "ui/framebuffer.h"

namespace skyblip::ui {

struct SixPackSnapshot {
    bool have_data{false};
    int32_t speed_kt{0};
    int32_t alt_ft{0};
    int32_t vs_fpm{0};
    uint16_t track_deg{0};
    int16_t turn_dps{0};  // degrees per second, positive = right
};

void draw_sixpack(Framebuffer& fb, const SixPackSnapshot& snap);

}  // namespace skyblip::ui

#endif
