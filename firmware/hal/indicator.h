// hal/indicator.h: capability port: the only thing on this device that can be
// seen from outside while it is running.
//
// E-paper holds its last image with the rails down, which is why the wordmark is
// painted before power off - so an off device and an on device look identical and
// the panel says nothing at all about whether there is a device there. This is
// the port that does.
//
// show() takes a LEVEL, not a pattern: exactly one lamp, or none, right now.
// The cadence is core/indication's, for the same reason the buzzer's cadence is
// core/annunciation's - a blink a driver plays is a blink nothing can test - and
// for one more: the duty cycle is the entire power argument for lighting an LED
// on a battery at all, and it has to be readable in one table rather than
// scattered across two platform adapters.
//
// park() is not show(Lamp::None). Dark is a pin driven to its inactive level,
// and on a board whose LEDs are active-low that level is HIGH: with the rail
// already collapsed, a pin held high sources current back into it through the
// LED. park() is dark AND the lines let go, and it is what the way down calls.
//
// This class IS the absent part. It is not abstract, so a board with no lamp
// fitted, or a devicetree with no LED node, is handed exactly this and lights
// nothing while running the same table. Same shape as hal/die_temperature.h and
// for the same reason: there is nothing to model, so a null part in runtime/
// would be a second file saying the same nothing.
#ifndef SKYBLIP_HAL_INDICATOR_H
#define SKYBLIP_HAL_INDICATOR_H

#include <cstdint>

namespace skyblip::hal {

// Which lamp is lit. One at a time and primaries only: two LEDs lit together to
// make a colour cost twice the current for a hue nobody can name through a
// diffuser, and the states are told apart by colour AND rhythm anyway
// (core/indication/lamp.h).
enum class Lamp : uint8_t { None, Green, Red, Blue };

class Indicator {
   public:
    virtual ~Indicator() = default;

    // Called only when the answer changes: an LED re-driven on every pass of the
    // loop is the same register write a hundred times a second for no light.
    virtual void show(Lamp lamp) { (void)lamp; }

    // Dark, and the pins released. After this the indicator is inert until the
    // next show().
    virtual void park() {}
};

}  // namespace skyblip::hal

#endif
