#include "ui/screens/altvs.h"

#include "core/util/format.h"

namespace skyblip::ui {

void draw_altvs(Framebuffer& fb, const AltVsSnapshot& snap) {
    fb.clear(true);
    fb.draw_text(4, 4, "ALT", true, 2);
    if (!snap.have_data) {
        fb.draw_text(20, 90, "---", true, 4);
        return;
    }

    char buf[16];
    int n = skyblip::fmt_int(buf, snap.alt_ft, 1, 0, true);
    buf[n] = 0;
    fb.draw_text(10, 60, buf, true, 4);
    fb.draw_text(10, 100, snap.imperial ? "FT" : "M", true, 2);

    int bar_x = 170, bar_y0 = 20, bar_h = 160;
    int mid = bar_y0 + bar_h / 2;
    fb.rect(bar_x, bar_y0, 20, bar_h, true, false);
    fb.hline(bar_x, mid, 20, true);
    int32_t vs = snap.vs_fpm;
    if (vs > 1000) vs = 1000;
    if (vs < -1000) vs = -1000;
    int len = static_cast<int>((vs * (bar_h / 2)) / 1000);
    if (len > 0)
        fb.rect(bar_x + 1, mid - len, 18, len, true, true);
    else if (len < 0)
        fb.rect(bar_x + 1, mid, 18, -len, true, true);

    n = skyblip::fmt_int(buf, snap.vs_fpm, 1, 0, false);
    buf[n] = 0;
    fb.draw_text(4, 150, buf, true, 1);
    fb.draw_text(4, 165, "FPM", true, 1);
}

}
