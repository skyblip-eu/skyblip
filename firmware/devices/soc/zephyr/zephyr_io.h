// devices/soc/zephyr/zephyr_io.h — Zephyr implementations of devices/io ports.
// Guarded by __ZEPHYR__ so the file is inert in the host test build. This is the
// SoC vendor zone ("the SoC is the only vendor zone"): the SX1262 / SSD1681
// drivers are UNCHANGED — they still talk to io::Spi / io::Gpio; only these
// adapters bind them to Zephyr's device API.
#ifndef SKYBLIP_DEVICES_SOC_ZEPHYR_ZEPHYR_IO_H
#define SKYBLIP_DEVICES_SOC_ZEPHYR_ZEPHYR_IO_H
#if defined(__ZEPHYR__)

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/uart.h>

#include "devices/io/io.h"

namespace skyblip::soc::zephyr {

// The drivers use a flat pin int (port*32 + pin, see boards/t_echo_plus/pins.h). nRF52840
// has two GPIO controllers (P0, P1); this maps the flat int onto (controller, pin)
// so the io::Gpio contract is unchanged — zero ripple into the shared drivers.
class ZephyrGpio : public io::Gpio {
   public:
    ZephyrGpio(const struct device* gpio0, const struct device* gpio1) : port_{gpio0, gpio1} {}

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

// Manual-CS SPI: the SX1262 driver drives CS itself via select(), so we keep CS
// out of the Zephyr spi_config and toggle the CS line here.
class ZephyrSpi : public io::Spi {
   public:
    ZephyrSpi(const struct device* spi, const struct spi_config& cfg, const struct device* cs_port,
              gpio_pin_t cs_pin)
        : spi_(spi), cfg_(cfg), cs_port_(cs_port), cs_pin_(cs_pin) {
        gpio_pin_configure(cs_port_, cs_pin_, GPIO_OUTPUT_HIGH);
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
    void select(bool on) override { gpio_pin_set(cs_port_, cs_pin_, on ? 0 : 1); }  // active-low

   private:
    const struct device* spi_;
    struct spi_config cfg_;
    const struct device* cs_port_;
    gpio_pin_t cs_pin_;
};

// Polling UART for the GNSS (L76K). Interrupt/async ingest is a later optimisation;
// polling is adequate at 9600–38400 baud drained every App::step().
class ZephyrUart : public io::Uart {
   public:
    explicit ZephyrUart(const struct device* uart) : uart_(uart) {}
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

}  // namespace skyblip::soc::zephyr
#endif  // __ZEPHYR__
#endif
