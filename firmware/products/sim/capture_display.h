// products/sim/capture_display.h — a hal::Display that just CAPTURES the last
// presented framebuffer (+ backlight / power state). Frontend-agnostic: the
// terminal frontend renders it as ASCII, the browser frontend copies it to a
// <canvas>. App drives it exactly as it drives the real SSD1681.
#ifndef SKYBLIP_PRODUCTS_SIM_CAPTURE_DISPLAY_H
#define SKYBLIP_PRODUCTS_SIM_CAPTURE_DISPLAY_H

#include "hal/display.h"
#include "ui/framebuffer.h"

namespace skyblip::sim {

class CaptureDisplay : public hal::Display {
   public:
    void present(const ui::Framebuffer& fb, hal::Rect, hal::Refresh mode) override {
        present_count++;
        last_mode = mode;
        if (powered_) last_ = fb;  // copy the 200x200 1-bit frame
    }
    void power_off() override { powered_ = false; }
    void set_backlight(bool on) override { backlight_ = on; }

    void power_on() { powered_ = true; }
    const ui::Framebuffer& framebuffer() const { return last_; }
    bool powered() const { return powered_; }
    bool backlight() const { return backlight_; }

    int present_count{0};
    hal::Refresh last_mode{hal::Refresh::Full};

   private:
    ui::Framebuffer last_{};
    bool powered_{true};
    bool backlight_{false};
};

}  // namespace skyblip::sim

#endif
