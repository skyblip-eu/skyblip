// hal/system_power.h: capability port: why the device came up, and how it goes
// away again. What the causes mean and when to use system_off() is decided in
// core/power; this is the call.
#ifndef SKYBLIP_HAL_SYSTEM_POWER_H
#define SKYBLIP_HAL_SYSTEM_POWER_H

#include "core/power/reset_reason.h"

namespace skyblip::hal {

class SystemPower {
   public:
    virtual ~SystemPower() = default;

    // Read once at boot and cleared there: a latching cause register reports
    // the same reason after every following reset until it is cleared.
    virtual power::ResetCause reset_causes() const { return power::ResetCause::None; }

    // Arm the wake source and drop the rails. Does not return on silicon.
    // core/power::ShutdownSequencer owns the precondition: the button must be
    // up before this is called.
    virtual void system_off() {}

    virtual void reboot() {}
};

}  // namespace skyblip::hal

#endif
