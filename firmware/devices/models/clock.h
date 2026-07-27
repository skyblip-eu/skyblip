// devices/models/clock.h — a model of time: driven directly instead of running,
// so every test and the simulator advance the clock deterministically.
#ifndef SKYBLIP_DEVICES_MODELS_CLOCK_H
#define SKYBLIP_DEVICES_MODELS_CLOCK_H

#include "hal/clock.h"

namespace skyblip::models {

class Clock : public hal::Clock {
   public:
    uint32_t millis() const override { return static_cast<uint32_t>(us_ / 1000); }
    uint64_t micros() const override { return us_; }
    void set_millis(uint32_t ms) { us_ = static_cast<uint64_t>(ms) * 1000; }
    void set_micros(uint64_t us) { us_ = us; }
    void advance(uint32_t ms) { us_ += static_cast<uint64_t>(ms) * 1000; }
    void advance_us(uint64_t us) { us_ += us; }

   private:
    uint64_t us_{0};
};

}

#endif
