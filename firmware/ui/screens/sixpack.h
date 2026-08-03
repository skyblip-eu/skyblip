// ui/screens/sixpack.h: the classic six-pack instrument panel, drawn from the
// GNSS-derived own-ship state: airspeed, attitude, altimeter over turn
// coordinator, heading, vertical speed.
//
// This is the page settings::units decides. The status page has room for two
// columns and shows the aeronautical figure and the SI one side by side; a dial
// has one needle and one number, so here the pilot's own habit has to be asked
// for. The snapshot stays in the aeronautical figures because the bank and
// flight-path geometry is worked in them; the graduation is chosen on the way
// to the glass.
#ifndef SKYBLIP_UI_SCREENS_SIXPACK_H
#define SKYBLIP_UI_SCREENS_SIXPACK_H

#include <cstdint>

#include "core/settings/settings.h"
#include "ui/framebuffer.h"

namespace skyblip::ui {

struct SixPackSnapshot {
    bool have_data{false};
    settings::Units units{settings::Units::Metric};
    int32_t speed_kt{0};
    int32_t alt_ft{0};
    int32_t vs_fpm{0};
    uint16_t track_deg{0};
    int16_t turn_dps{0};  // degrees per second, positive = right
};

void draw_sixpack(Framebuffer& fb, const SixPackSnapshot& snap);

}  // namespace skyblip::ui

#endif
