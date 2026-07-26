#include "devices/host/fake_display.h"

#include <cstdio>
#include <cstring>

namespace skyblip::host {

void FakeDisplay::present(const ui::Framebuffer& fb, hal::Rect region, hal::Refresh mode) {
    std::memcpy(last_fb_.data(), fb.data(), ui::Framebuffer::kBytes);
    last_region = region;
    last_mode = mode;
    present_count++;
    powered_ = true;
}

bool FakeDisplay::save_pgm(const char* path) const {
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

}
