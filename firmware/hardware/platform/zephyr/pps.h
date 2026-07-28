#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_PPS_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_PPS_H
#if defined(__ZEPHYR__)

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

namespace skyblip::platform::zephyr {

// The UTC-second phase, latched in the GPIO interrupt rather than derived from a
// kernel tick: the slot map is specified against the PPS edge, and a task-level
// timestamp carries the tick jitter the 5 ms guard exists to absorb.
class Pps {
   public:
    Status begin() {
        if (!device_is_ready(pin_.port)) return Status::Down;
        if (gpio_pin_configure_dt(&pin_, GPIO_INPUT) != 0) return Status::Down;
        gpio_init_callback(&cb_, on_edge, BIT(pin_.pin));
        if (gpio_add_callback(pin_.port, &cb_) != 0) return Status::Down;
        if (gpio_pin_interrupt_configure_dt(&pin_, GPIO_INT_EDGE_RISING) != 0) return Status::Down;
        self_ = this;
        return Status::Ok;
    }

    bool locked() const {
        return edge_us_ != 0 && k_ticks_to_us_floor64(k_uptime_ticks()) - edge_us_ < kHoldoverUs;
    }

    uint32_t ms_since(uint64_t now_us) const {
        if (edge_us_ == 0) return 0;
        return static_cast<uint32_t>((now_us - edge_us_) / 1000);
    }

    uint64_t last_edge_us() const { return edge_us_; }
    uint32_t edges() const { return edges_; }

   private:
    static constexpr uint64_t kHoldoverUs = 3000000;

    static void on_edge(const struct device*, struct gpio_callback*, gpio_port_pins_t) {
        if (self_ == nullptr) return;
        self_->edge_us_ = k_ticks_to_us_floor64(k_uptime_ticks());
        self_->edges_++;
    }

    static Pps* self_;

    struct gpio_dt_spec pin_ {
        GPIO_DT_SPEC_GET(DT_ALIAS(gnss_pps), gpios)
    };
    struct gpio_callback cb_ {};
    volatile uint64_t edge_us_{0};
    volatile uint32_t edges_{0};
};

inline Pps* Pps::self_ = nullptr;

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
