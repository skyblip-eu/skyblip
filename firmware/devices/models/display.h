// devices/models/display.h — a model of the e-paper at the hal::Display seam:
// it keeps the last presented frame, the refresh mode, and the panel/backlight
// power state. The panel driver is NOT exercised here (models/ssd1681.h does
// that); this is the fast path App and the UI are driven against, and the frame
// the simulator frontends paint.
#ifndef SKYBLIP_DEVICES_MODELS_DISPLAY_H
#define SKYBLIP_DEVICES_MODELS_DISPLAY_H

#include "hal/display.h"
#include "ui/framebuffer.h"

namespace skyblip::models {

class Display : public hal::Display {
   public:
    void present(const ui::Framebuffer& fb, hal::Rect region, hal::Refresh mode) override;
    void power_off() override { powered_ = false; }
    void set_backlight(bool on) override { backlight_ = on; }

    void power_on() { powered_ = true; }

    const ui::Framebuffer& framebuffer() const { return last_fb_; }
    bool powered() const { return powered_; }
    bool backlight() const { return backlight_; }

    bool save_pgm(const char* path) const;

    int present_count{0};
    hal::Rect last_region{};
    hal::Refresh last_mode{hal::Refresh::Full};

   private:
    ui::Framebuffer last_fb_{};
    bool powered_{true};
    bool backlight_{false};
};

}  // namespace skyblip::models

#endif
