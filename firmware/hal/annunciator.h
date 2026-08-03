// hal/annunciator.h: capability port: buzzer + vibration motor (T-Echo Plus).
//
// alarm() opens a CONTINUOUS tone and it runs until silence(): there is no
// cadence in the port, no timer behind it and no duration argument, because a
// pattern a driver plays is a pattern nothing can test. On times, off times,
// repeats and re-announcement are core/annunciation's, driven by exactly one
// service - which is also the one that has to release the tone when the level
// falls, the target goes, the pilot switches alarms off or the device goes down.
// vibrate() is the exception: it is a single pulse of a stated length and the
// implementation owns its end, because a motor left on is a flat battery.
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
