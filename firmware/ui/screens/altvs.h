// ui/screens/altvs.h — altitude + vertical-speed page (1-USE-CASES #4, v1 target).
#ifndef SKYBLIP_UI_SCREENS_ALTVS_H
#define SKYBLIP_UI_SCREENS_ALTVS_H

#include <cstdint>

#include "ui/framebuffer.h"

namespace skyblip::ui {

struct AltVsSnapshot {
    bool have_data{false};
    int32_t alt_ft{0};
    int32_t vs_fpm{0};
    bool imperial{true};
};

void draw_altvs(Framebuffer& fb, const AltVsSnapshot& snap);

}

#endif
