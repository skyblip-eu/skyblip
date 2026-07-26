// hal/display.h — capability port: present a 1-bit framebuffer region to the
#ifndef SKYBLIP_HAL_DISPLAY_H
#define SKYBLIP_HAL_DISPLAY_H

#include <cstdint>

namespace skyblip::ui {
class Framebuffer;
}

namespace skyblip::hal {

enum class Refresh : uint8_t { Full, Partial };

struct Rect {
    uint16_t x, y, w, h;
};

class Display {
   public:
    virtual ~Display() = default;
    virtual void present(const ui::Framebuffer& fb, Rect region, Refresh mode) = 0;
    virtual void power_off() = 0;
    // Optional backlight/panel-enable (T-Echo Plus P1.11). Default: no-op.
    virtual void set_backlight(bool /*on*/) {}
};

}

#endif
