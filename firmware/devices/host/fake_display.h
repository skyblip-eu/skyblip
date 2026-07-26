// devices/host/fake_display.h — hal::Display double that records the last
#ifndef SKYBLIP_DEVICES_HOST_FAKE_DISPLAY_H
#define SKYBLIP_DEVICES_HOST_FAKE_DISPLAY_H

#include "hal/display.h"
#include "ui/framebuffer.h"

namespace skyblip::host {

class FakeDisplay : public hal::Display {
   public:
    void present(const ui::Framebuffer& fb, hal::Rect region, hal::Refresh mode) override;
    void power_off() override { powered_ = false; }

    bool save_pgm(const char* path) const;

    int present_count{0};
    hal::Rect last_region{};
    hal::Refresh last_mode{hal::Refresh::Full};

   private:
    ui::Framebuffer last_fb_;
    bool powered_{true};
};

}

#endif
