#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_INDICATOR_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_INDICATOR_H
#if defined(__ZEPHYR__)

#include <zephyr/drivers/gpio.h>

#include "hal/indicator.h"

namespace skyblip::platform::zephyr {

// The board's RGB status LEDs, one lamp at a time. Whether there are any at all
// is a devicetree question: the specs come from GPIO_DT_SPEC_GET_OR, so a board
// file with no LED node yields a null port, ready() answers false, the platform
// grants no capability and the product never attaches this - the same table runs
// and nothing lights.
class Indicator : public hal::Indicator {
   public:
    Indicator(const struct gpio_dt_spec& green, const struct gpio_dt_spec& red,
              const struct gpio_dt_spec& blue)
        : green_(green), red_(red), blue_(blue) {}

    bool ready() const {
        return gpio_is_ready_dt(&green_) && gpio_is_ready_dt(&red_) && gpio_is_ready_dt(&blue_);
    }

    void begin() {
        if (ready()) show(hal::Lamp::None);
    }

    void show(hal::Lamp lamp) override {
        drive(green_, lamp == hal::Lamp::Green);
        drive(red_, lamp == hal::Lamp::Red);
        drive(blue_, lamp == hal::Lamp::Blue);
    }

    // Dark AND released. These LEDs hang off the gated peripheral rail - SoftRF's
    // own comment on that pin reads "Power: EINK, RGB, CN1" (platform/iomap/
    // LilyGO_TEcho.h:79-80) - and they are active-low, so "off" is the pin driven
    // HIGH. A pin held high once the rail has collapsed sources current back into
    // it through the LED, which is why SoftRF switches its LEDs off and then to
    // inputs in SoC_fini (platform/nRF52.cpp:2799-2807) and why this is a step of
    // its own rather than show(Lamp::None).
    //
    // GPIO_DISCONNECTED rather than GPIO_INPUT: an input leaves the pin's input
    // buffer powered, which is the microamps this whole exercise is about. The
    // next show() reconfigures as an output, so park() is not one-way.
    void park() override {
        show(hal::Lamp::None);
        release(green_);
        release(red_);
        release(blue_);
    }

   private:
    // Reconfigured on every call rather than set: it is one register write either
    // way on this SoC, and it is what makes park() reversible without a flag.
    static void drive(const struct gpio_dt_spec& pin, bool on) {
        if (pin.port == nullptr) return;
        gpio_pin_configure_dt(&pin, on ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE);
    }

    static void release(const struct gpio_dt_spec& pin) {
        if (pin.port == nullptr) return;
        gpio_pin_configure_dt(&pin, GPIO_DISCONNECTED);
    }

    struct gpio_dt_spec green_;
    struct gpio_dt_spec red_;
    struct gpio_dt_spec blue_;
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
