#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_SYSTEM_POWER_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_SYSTEM_POWER_H

#include "hal/system_power.h"

namespace skyblip::platform::host {

// The rails a test can watch go down, and a cause register it can load with
// whatever the field would have handed us.
class SystemPower : public hal::SystemPower {
   public:
    power::ResetCause reset_causes() const override { return causes; }
    void system_off() override { offs++; }
    void reboot() override { reboots++; }

    power::ResetCause causes{power::ResetCause::PowerOn};
    int offs{0};
    int reboots{0};
};

}  // namespace skyblip::platform::host

#endif
