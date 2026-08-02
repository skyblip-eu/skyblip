#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_SYSTEM_POWER_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_SYSTEM_POWER_H
#if defined(__ZEPHYR__)

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>

#include "hal/system_power.h"

namespace skyblip::platform::zephyr {

class SystemPower : public hal::SystemPower {
   public:
    explicit SystemPower(const struct gpio_dt_spec& wake) : wake_(wake) {}

    // Once, at boot. The register latches, so leaving a cause in it makes every
    // later boot report the same reason.
    void begin() {
        uint32_t flags = 0;
        if (hwinfo_get_reset_cause(&flags) != 0) return;
        causes_ = translate(flags);
        hwinfo_clear_reset_cause();
    }

    power::ResetCause reset_causes() const override { return causes_; }

    void system_off() override {
        // INFO: hk 02aug26 the nRF52 wakes from SYSTEM OFF on GPIO SENSE, which
        // is a level detect and not an edge. Zephyr's nRF GPIO driver maps
        // GPIO_INT_LEVEL_ACTIVE onto it (samples/boards/nordic/system_off), so
        // this is the wake pin being armed. The caller must already have waited
        // for the button to be released; arming it under a held button wakes the
        // device the instant the rails drop.
        gpio_pin_configure_dt(&wake_, GPIO_INPUT);
        gpio_pin_interrupt_configure_dt(&wake_, GPIO_INT_LEVEL_ACTIVE);
        sys_poweroff();
    }

    void reboot() override { sys_reboot(SYS_REBOOT_WARM); }

   private:
    static power::ResetCause translate(uint32_t flags) {
        // INFO: hk 02aug26 the nRF52 leaves RESETREAS all-zero after a power-on
        // or brown-out reset: it latches only the other seven causes (nRF52840
        // PS v1.8 §5.3.3). An empty register is therefore the power-on case, not
        // an unread one.
        if (flags == 0) return power::ResetCause::PowerOn;

        power::ResetCause causes = power::ResetCause::None;
        if (flags & RESET_POR) causes |= power::ResetCause::PowerOn;
        if (flags & RESET_PIN) causes |= power::ResetCause::Pin;
        if (flags & RESET_BROWNOUT) causes |= power::ResetCause::Brownout;
        if (flags & RESET_SOFTWARE) causes |= power::ResetCause::Software;
        if (flags & RESET_WATCHDOG) causes |= power::ResetCause::Watchdog;
        if (flags & RESET_CPU_LOCKUP) causes |= power::ResetCause::Lockup;
        if (flags & RESET_LOW_POWER_WAKE) causes |= power::ResetCause::LowPowerWake;
        if (flags & RESET_DEBUG) causes |= power::ResetCause::Debug;
        return causes;
    }

    struct gpio_dt_spec wake_;
    power::ResetCause causes_{power::ResetCause::None};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
