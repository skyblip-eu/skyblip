// hal/clock.h — capability port: a monotonic clock core/ can read.
#ifndef SKYBLIP_HAL_CLOCK_H
#define SKYBLIP_HAL_CLOCK_H

#include <cstdint>

namespace skyblip::hal {

class Clock {
   public:
    virtual ~Clock() = default;
    virtual uint32_t millis() const = 0;
    virtual uint64_t micros() const = 0;
};

}

#endif
