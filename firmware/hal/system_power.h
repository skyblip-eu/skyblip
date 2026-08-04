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

    // Walk core/power's power-down order, arm the wake source and drop the
    // rails. Does not return on silicon. core/power::ShutdownSequencer owns the
    // precondition: the button must be up before this is called, and
    // core/power::kPowerDownOrder owns what happens in which order once it is.
    virtual void system_off() {}

    virtual void reboot() {}

    // THE POWER-FAILURE COMPARATOR.
    //
    // Absence is a capability and it is reported here, by the one object that
    // could raise the warning: a platform with no comparator answers false to
    // armed() and never raises a warning, so core/power's rule
    // (core/power/cutoff.h::may_write) degrades to the voltage half by itself and
    // no service branches on a pointer or a flag. It lives on this port rather
    // than on a role of its own because it is the same peripheral: on the
    // nRF52840 RESETREAS, SYSTEMOFF and POFCON are three registers of POWER, and
    // hal::Roles is assembled by the board, which has no way to probe a
    // comparator that has no pin.
    virtual bool supply_monitor_armed() const { return false; }

    // Latched by the comparator's interrupt, read and cleared here. Returns true
    // once per warning, because what reads it hands it to core/power, which
    // latches it for good.
    virtual bool take_supply_warning() { return false; }
};

}  // namespace skyblip::hal

#endif
