#ifndef SKYBLIP_HAL_DISPLAY_H
#define SKYBLIP_HAL_DISPLAY_H

#include <cstdint>

namespace skyblip::ui {
class Framebuffer;
}

namespace skyblip::hal {

// Full drives every pixel through the flashing waveform and clears ghosting.
// Fast drives only the pixels that differ from the previous frame (the panel
// diffs its two RAM banks), flicker-free.
enum class Refresh : uint8_t { Full, Fast };

class Display {
   public:
    virtual ~Display() = default;
    // Non-blocking: starts the refresh and returns. Call only when ready().
    virtual void present(const ui::Framebuffer& fb, Refresh mode, uint32_t now_ms) = 0;
    // True when the panel can accept a present(). Polling this is also what
    // finishes a refresh (panel to sleep) once the glass settles.
    virtual bool ready(uint32_t /*now_ms*/) { return true; }
    virtual void power_off() = 0;
    virtual void power_on() {}
    virtual void set_backlight(bool /*on*/) {}
};

}

#endif
