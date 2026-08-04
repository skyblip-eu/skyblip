// hal/haptic.h: capability port: the thing that shakes.
//
// Split out of hal/annunciator.h because a pulse of the motor is not one kind of
// hardware. A bare vibration motor on a GPIO is switched on and off by the pin;
// a DRV2605 on I2C is a waveform driver whose enable pin only brings it out of
// standby, and which produces nothing at all until it has been given a mode and
// a drive value. Both are "the device shakes"; neither is a null check at the
// call site, and neither is a duration.
//
// start() begins driving and stop() ends it: no length, because the length of a
// pulse is a policy (products/skyblip_go/services/alarm.h owns it) and the timer
// that ends it belongs to whoever can hold one - the annunciator adapter on each
// platform. An implementation may idle the part between pulses; nothing above
// this line knows or cares.
#ifndef SKYBLIP_HAL_HAPTIC_H
#define SKYBLIP_HAL_HAPTIC_H

namespace skyblip::hal {

class Haptic {
   public:
    virtual ~Haptic() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
};

}  // namespace skyblip::hal

#endif
