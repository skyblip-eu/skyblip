#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_IO_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_IO_H
#if defined(__ZEPHYR__)

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/uart.h>

#include "hardware/io/io.h"

namespace skyblip::platform::zephyr {

// The drivers use a flat pin int (port*32 + pin). nRF52840 has two GPIO
// controllers, so the flat int maps onto (controller, pin).
class Gpio : public io::Gpio {
   public:
    Gpio(const struct device* gpio0, const struct device* gpio1) : port_{gpio0, gpio1} {}

    void set(int pin, bool level) override { gpio_pin_set(dev(pin), bit(pin), level ? 1 : 0); }
    bool get(int pin) override { return gpio_pin_get(dev(pin), bit(pin)) == 1; }
    void mode_output(int pin) override { gpio_pin_configure(dev(pin), bit(pin), GPIO_OUTPUT); }
    void mode_input(int pin, bool pullup) override {
        gpio_pin_configure(dev(pin), bit(pin), GPIO_INPUT | (pullup ? GPIO_PULL_UP : 0));
    }

   private:
    const struct device* dev(int pin) const { return port_[(pin >> 5) & 1]; }
    static gpio_pin_t bit(int pin) { return static_cast<gpio_pin_t>(pin & 31); }
    const struct device* port_[2];
};

// Manual-CS SPI: the drivers drive CS themselves via select(), so CS stays out
// of the Zephyr spi_config and the line is toggled here.
class Spi : public io::Spi {
   public:
    Spi(const struct device* spi, const struct spi_config& cfg, const struct gpio_dt_spec& cs)
        : spi_(spi), cfg_(cfg), cs_(cs) {
        gpio_pin_configure_dt(&cs_, GPIO_OUTPUT_INACTIVE);
    }

    void transfer(const uint8_t* tx, uint8_t* rx, size_t len) override {
        static uint8_t zeros[64] = {0};
        const uint8_t* txp = tx ? tx : zeros;  // clock zeros when tx==nullptr
        struct spi_buf tb{const_cast<uint8_t*>(txp), len};
        struct spi_buf rb{rx, len};
        struct spi_buf_set txs{&tb, 1};
        struct spi_buf_set rxs{&rb, 1};
        spi_transceive(spi_, &cfg_, &txs, rx ? &rxs : nullptr);
    }
    void select(bool on) override { gpio_pin_set_dt(&cs_, on ? 1 : 0); }

   private:
    const struct device* spi_;
    struct spi_config cfg_;
    struct gpio_dt_spec cs_;
};

class Uart : public io::Uart {
   public:
    explicit Uart(const struct device* uart) : uart_(uart) {}
    size_t write(const uint8_t* data, size_t len) override {
        for (size_t i = 0; i < len; i++) uart_poll_out(uart_, data[i]);
        return len;
    }
    size_t read(uint8_t* data, size_t cap) override {
        size_t n = 0;
        while (n < cap && uart_poll_in(uart_, &data[n]) == 0) n++;
        return n;
    }
    size_t available() override { return 0; }

   private:
    const struct device* uart_;
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
