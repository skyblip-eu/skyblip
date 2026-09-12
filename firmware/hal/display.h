#ifndef SKYBLIP_HAL_DISPLAY_H
#define SKYBLIP_HAL_DISPLAY_H

#include <cstdint>

namespace skyblip::ui {
class Framebuffer;
}

namespace skyblip::hal {

// INFO: fc 12sep26 the two the controller has: SSD1681 display mode 1 and mode 2 (0x22 bit 3)
enum class Refresh : uint8_t { Full, Partial };

class Display {
   public:
    virtual ~Display() = default;
    // INFO: fc 01aug25 present() is non-blocking, so call it only when ready() is true
    virtual void present(const ui::Framebuffer& fb, Refresh mode, uint32_t now_ms) = 0;
    virtual bool ready(uint32_t /*now_ms*/) { return true; }
    virtual void power_off() = 0;
    virtual void power_on() {}
    virtual void set_backlight(bool /*on*/) {}
};

}

#endif
