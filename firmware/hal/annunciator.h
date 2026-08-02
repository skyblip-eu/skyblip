// hal/annunciator.h: capability port: buzzer + vibration motor (T-Echo Plus).
#ifndef SKYBLIP_HAL_ANNUNCIATOR_H
#define SKYBLIP_HAL_ANNUNCIATOR_H

#include <cstdint>

namespace skyblip::hal {

class Annunciator {
   public:
    virtual ~Annunciator() = default;
    virtual void alarm(uint8_t level, uint8_t volume) = 0;
    virtual void vibrate(uint16_t ms) = 0;
    virtual void silence() = 0;
};

}

#endif
