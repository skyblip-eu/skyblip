#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_ANNUNCIATOR_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_ANNUNCIATOR_H
#if defined(__ZEPHYR__)

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>

#include "hal/annunciator.h"

namespace skyblip::platform::zephyr {

class Annunciator : public hal::Annunciator {
   public:
    Annunciator(const struct pwm_dt_spec& buzzer, const struct gpio_dt_spec& vibro)
        : buzzer_(buzzer), vibro_(vibro) {}

    void begin() {
        gpio_pin_configure_dt(&vibro_, GPIO_OUTPUT_INACTIVE);
        k_timer_init(&vibro_timer_, vibro_off, nullptr);
        k_timer_user_data_set(&vibro_timer_, this);
        silence();
    }

    // Opens the tone and leaves it open, per hal/annunciator.h: whoever calls
    // this owes it a silence(). The pattern is core/annunciation's.
    void alarm(uint8_t level, uint8_t volume) override {
        if (level == 0 || volume == 0) return silence();
        uint32_t period = PWM_HZ(tone_hz(level));
        pwm_set_dt(&buzzer_, period, period / (5 - (volume > 3 ? 3 : volume)));
    }
    void vibrate(uint16_t ms) override {
        gpio_pin_set_dt(&vibro_, 1);
        k_timer_start(&vibro_timer_, K_MSEC(ms), K_NO_WAIT);
    }
    void silence() override { pwm_set_dt(&buzzer_, PWM_HZ(kTransducerResonanceHz), 0); }

   private:
    // INFO: fc 03aug26 The fitted transducer is not identified in LilyGO's own
    // material (project/research/alarm-audibility.md). It is driven straight off
    // one PWM pin with no transistor, which is a passive piezo, and commodity
    // 9x9 mm SMD piezo sounders cluster at 4000 Hz +/- 500 Hz. The table this
    // replaced ran 2400 to 3200 Hz, entirely below that band, with the loudest
    // level furthest from resonance. The bench sweep in that document is what
    // settles the figure; until then the three levels bracket the cluster.
    static constexpr uint32_t kTransducerResonanceHz = 4000;
    static constexpr uint32_t kLevelStepHz = 400;
    static constexpr uint8_t kMiddleLevel = 2;

    static uint32_t tone_hz(uint8_t level) {
        return kTransducerResonanceHz + (static_cast<int32_t>(level) - kMiddleLevel) * kLevelStepHz;
    }

    static void vibro_off(struct k_timer* t) {
        auto* self = static_cast<Annunciator*>(k_timer_user_data_get(t));
        gpio_pin_set_dt(&self->vibro_, 0);
    }
    struct pwm_dt_spec buzzer_;
    struct gpio_dt_spec vibro_;
    struct k_timer vibro_timer_;
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
