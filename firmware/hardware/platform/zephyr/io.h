#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_IO_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_IO_H
#if defined(__ZEPHYR__)

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
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

// The sensor bus. Zephyr's own drivers (the barometer) share it through the same
// device, which is safe: i2c_transfer takes the bus's lock.
//
// A zero-length write is an address probe and nothing else, which is how a scan
// asks "is anything there": it is what Zephyr's own i2c shell does
// (drivers/i2c/i2c_shell.c, an I2C_MSG_WRITE | I2C_MSG_STOP message of length 0).
// UNBUILT HERE: on nRF TWIM a zero-length transfer is handled by the driver
// rather than the peripheral, so this is the one call in the class that has to be
// confirmed on silicon.
class I2c : public io::I2c {
   public:
    explicit I2c(const struct device* bus) : bus_(bus) {}

    bool write(uint8_t addr, const uint8_t* data, size_t len) override {
        if (!device_is_ready(bus_)) return false;
        if (len == 0) {
            uint8_t nothing = 0;
            struct i2c_msg msg{&nothing, 0, I2C_MSG_WRITE | I2C_MSG_STOP};
            return i2c_transfer(bus_, &msg, 1, addr) == 0;
        }
        return i2c_write(bus_, data, len, addr) == 0;
    }

    bool read(uint8_t addr, uint8_t* data, size_t len) override {
        if (!device_is_ready(bus_)) return false;
        return i2c_read(bus_, data, len, addr) == 0;
    }

   private:
    const struct device* bus_;
};

class Uart : public io::Uart, public io::UartRate {
   public:
    explicit Uart(const struct device* uart) : uart_(uart) {}

    // Retuning the port, which is what makes the L76K's autobaud recovery more
    // than a table: a receiver that comes up at a rate the devicetree did not
    // expect leaves the unit silently GNSS-less, and the driver can only walk its
    // candidates if something can move the UART with it. False means the port
    // refused, which is a capability we do not have rather than a call that
    // quietly did nothing.
    bool set(uint32_t baud) override {
        struct uart_config config{};
        if (uart_config_get(uart_, &config) != 0) return false;
        if (config.baudrate == baud) return true;
        config.baudrate = baud;
        return uart_configure(uart_, &config) == 0;
    }
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
