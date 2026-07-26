// devices/soc/zephyr/zephyr_annunciator.h — hal::Annunciator: PWM buzzer +
// GPIO vibration motor (T-Echo Plus). Vibration auto-stops via a kernel timer.
#ifndef SKYBLIP_DEVICES_SOC_ZEPHYR_ZEPHYR_ANNUNCIATOR_H
#define SKYBLIP_DEVICES_SOC_ZEPHYR_ZEPHYR_ANNUNCIATOR_H
#if defined(__ZEPHYR__)

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>

#include "hal/annunciator.h"

namespace skyblip::soc::zephyr {

class ZephyrAnnunciator : public hal::Annunciator {
   public:
    ZephyrAnnunciator(const struct pwm_dt_spec& buzzer, const struct gpio_dt_spec& vibro)
        : buzzer_(buzzer), vibro_(vibro) {
        gpio_pin_configure_dt(&vibro_, GPIO_OUTPUT_INACTIVE);
        k_timer_init(&vibro_timer_, vibro_off, nullptr);
        k_timer_user_data_set(&vibro_timer_, this);
    }

    void alarm(uint8_t level, uint8_t volume) override {
        if (level == 0 || volume == 0) return silence();
        // Higher level → higher pitch; volume → duty cycle.
        uint32_t period = PWM_HZ(2000 + level * 400);
        pwm_set_dt(&buzzer_, period, period / (5 - (volume > 3 ? 3 : volume)));
    }
    void vibrate(uint16_t ms) override {
        gpio_pin_set_dt(&vibro_, 1);
        k_timer_start(&vibro_timer_, K_MSEC(ms), K_NO_WAIT);
    }
    void silence() override { pwm_set_dt(&buzzer_, PWM_HZ(2000), 0); }

   private:
    static void vibro_off(struct k_timer* t) {
        auto* self = static_cast<ZephyrAnnunciator*>(k_timer_user_data_get(t));
        gpio_pin_set_dt(&self->vibro_, 0);
    }
    struct pwm_dt_spec buzzer_;
    struct gpio_dt_spec vibro_;
    struct k_timer vibro_timer_;
};

}  // namespace skyblip::soc::zephyr
#endif  // __ZEPHYR__
#endif
