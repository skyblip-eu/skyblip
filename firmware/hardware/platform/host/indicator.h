#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_INDICATOR_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_INDICATOR_H

#include <cstdint>

#include "hal/indicator.h"

namespace skyblip::platform::host {

// The lamp a test can watch. It counts show() calls as well as recording the
// lamp, because the difference between a wink and an LED re-driven on every pass
// of the loop is exactly that count - and the second one is a device that spends
// a register write a hundred times a second on no light.
class Indicator : public hal::Indicator {
   public:
    void show(hal::Lamp lamp) override {
        lamp_ = lamp;
        shows_++;
        if (lamp != hal::Lamp::None) lightings_++;
        parked_ = false;
    }

    void park() override {
        lamp_ = hal::Lamp::None;
        parked_ = true;
        parks_++;
    }

    hal::Lamp lamp() const { return lamp_; }
    bool lit() const { return lamp_ != hal::Lamp::None; }
    // True once park() has run and until the next show(). The silicon adapter has
    // let go of the pins at that point; here it is the fact a test asserts.
    bool parked() const { return parked_; }
    uint32_t shows() const { return shows_; }
    uint32_t lightings() const { return lightings_; }
    uint32_t parks() const { return parks_; }

   private:
    hal::Lamp lamp_{hal::Lamp::None};
    uint32_t shows_{0};
    uint32_t lightings_{0};
    uint32_t parks_{0};
    bool parked_{false};
};

}  // namespace skyblip::platform::host

#endif
