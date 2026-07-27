// devices/soc/zephyr/zephyr_annunciator.h — hal::Annunciator: PWM buzzer +
// GPIO vibration motor (T-Echo Plus). Vibration auto-stops via a kernel timer.
#ifndef SKYBLIP_DEVICES_SOC_ZEPHYR_ZEPHYR_ANNUNCIATOR_H
#define SKYBLIP_DEVICES_SOC_ZEPHYR_ZEPHYR_ANNUNCIATOR_H
#if defined(__ZEPHYR__)

#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>

#include "devices/io/io.h"
#include "hal/annunciator.h"

namespace skyblip::soc::zephyr {

// The buzzer needs a period and a duty cycle, so it comes from devicetree (the
// `buzzer` alias -> pwm0). The vibration motor is a plain output, so it goes
// through io::Gpio and the shared pin map like every other GPIO on this board -
// no second source of truth for pin numbers.
class ZephyrAnnunciator : public hal::Annunciator {
   public:
    ZephyrAnnunciator(const struct pwm_dt_spec& buzzer, io::Gpio& gpio, int vibro_pin)
        : buzzer_(buzzer), gpio_(gpio), vibro_pin_(vibro_pin) {}

    // Separate from the constructor on purpose: the board struct is constructed
    // before device_is_ready() has been checked, and configuring a pin on a
    // not-yet-ready GPIO controller is exactly the kind of bring-up order bug
    // that only shows up as a dead buzzer on one unit.
    void begin() {
        gpio_.mode_output(vibro_pin_);
        gpio_.set(vibro_pin_, false);
        k_timer_init(&vibro_timer_, vibro_off, nullptr);
        k_timer_user_data_set(&vibro_timer_, this);
        silence();
    }

    void alarm(uint8_t level, uint8_t volume) override {
        if (level == 0 || volume == 0) return silence();
        // Higher level → higher pitch; volume → duty cycle.
        uint32_t period = PWM_HZ(2000 + level * 400);
        pwm_set_dt(&buzzer_, period, period / (5 - (volume > 3 ? 3 : volume)));
    }
    void vibrate(uint16_t ms) override {
        gpio_.set(vibro_pin_, true);
        k_timer_start(&vibro_timer_, K_MSEC(ms), K_NO_WAIT);
    }
    void silence() override { pwm_set_dt(&buzzer_, PWM_HZ(2000), 0); }

   private:
    static void vibro_off(struct k_timer* t) {
        auto* self = static_cast<ZephyrAnnunciator*>(k_timer_user_data_get(t));
        self->gpio_.set(self->vibro_pin_, false);
    }
    struct pwm_dt_spec buzzer_;
    io::Gpio& gpio_;
    int vibro_pin_;
    struct k_timer vibro_timer_;
};

}  // namespace skyblip::soc::zephyr
#endif  // __ZEPHYR__
#endif
