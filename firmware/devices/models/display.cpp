#include "devices/models/display.h"

#include <cstdio>

namespace skyblip::models {

void Display::present(const ui::Framebuffer& fb, hal::Rect region, hal::Refresh mode) {
    last_region = region;
    last_mode = mode;
    present_count++;
    // An unpowered panel keeps showing whatever was on it: e-paper is bistable.
    if (powered_) last_fb_ = fb;
}

bool Display::save_pgm(const char* path) const {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P5\n%d %d\n255\n", ui::Framebuffer::kW, ui::Framebuffer::kH);
    for (int y = 0; y < ui::Framebuffer::kH; y++) {
        for (int x = 0; x < ui::Framebuffer::kW; x++) {
            uint8_t v = last_fb_.get_pixel(x, y) ? 0 : 255;
            std::fwrite(&v, 1, 1, f);
        }
    }
    std::fclose(f);
    return true;
}

}  // namespace skyblip::models
