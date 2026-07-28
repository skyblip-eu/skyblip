#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_PLATFORM_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_PLATFORM_H
#if defined(__ZEPHYR__)

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>

#include "hal/capabilities.h"
#include "hardware/platform/zephyr/annunciator.h"
#include "hardware/platform/zephyr/baro.h"
#include "hardware/platform/zephyr/clock.h"
#include "hardware/platform/zephyr/dfu.h"
#include "hardware/platform/zephyr/io.h"
#include "hardware/platform/zephyr/kvstore.h"
#include "hardware/platform/zephyr/link.h"
#include "hardware/platform/zephyr/pps.h"
#include "hardware/platform/zephyr/rf.h"

namespace skyblip::platform::zephyr {

class Platform {
   public:
    using Rf = zephyr::Rf;
    using Link = zephyr::Link;

    Status begin() {
        if (!device_is_ready(gpio0_) || !device_is_ready(gpio1_)) return Status::Down;
        if (!device_is_ready(radio_spi_dev_)) return Status::Down;
        // The gated rails are raised at board level so MCUboot sees them too;
        // what is still owed here is the SX1262 TCXO settling time.
        k_msleep(50);
        kv_.begin();
        annunciator_.begin();
        link_.begin();
        baro_ = baro76_.ready() ? &baro76_ : (baro77_.ready() ? &baro77_ : nullptr);
        gpio_pin_configure_dt(&button_, GPIO_INPUT);
        pps_.begin();
        return Status::Ok;
    }

    void wire(const io::PinMap&) {}

    io::Spi& spi(io::BusId id) {
        if (id == io::BusId::Epd) return epd_spi_;
        return radio_spi_;
    }
    io::Uart& uart(io::BusId) { return gnss_uart_; }
    io::Gpio& gpio() { return gpio_; }

    zephyr::Clock& clock() { return clock_; }
    zephyr::Link& link() { return link_; }
    zephyr::KvStore& kv() { return kv_; }
    zephyr::Annunciator& annunciator() { return annunciator_; }
    zephyr::Dfu& dfu() { return dfu_; }
    zephyr::Pps& pps() { return pps_; }
    zephyr::Baro* baro() { return baro_; }

    bool button_down() { return gpio_pin_get_dt(&button_) == 1; }
    bool read_pressure_pa(uint32_t& out_pa) {
        return baro_ != nullptr && baro_->read_pressure_pa(out_pa);
    }

    // Probe only: what silicon answered, before anything is brought up.
    hal::Capabilities capabilities() const {
        hal::Capabilities c = hal::Capability::Buzzer | hal::Capability::Vibro |
                              hal::Capability::Storage | hal::Capability::Dfu |
                              hal::Capability::Button | hal::Capability::Link;
        if (device_is_ready(epd_spi_dev_)) c |= hal::Capability::Display;
        if (device_is_ready(gnss_uart_dev_)) c |= hal::Capability::Gnss;
        if (device_is_ready(baro76_dev_) || device_is_ready(baro77_dev_))
            c |= hal::Capability::Baro;
        return c;
    }

    // SoC unique id -> 24-bit default ADS-L address.
    uint32_t device_addr() const {
        uint8_t id[8] = {0};
        const ssize_t n = hwinfo_get_device_id(id, sizeof(id));
        uint32_t a = 0;
        for (ssize_t i = 0; i < 3 && i < n; i++) a = (a << 8) | id[i];
        return a & 0x00FFFFFFu;
    }

   private:
    static constexpr struct spi_config kSpiCfg = {
        .frequency = 8000000U,
        .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER,
        .slave = 0,
        .cs = {},
    };

    const struct device* gpio0_{DEVICE_DT_GET(DT_NODELABEL(gpio0))};
    const struct device* gpio1_{DEVICE_DT_GET(DT_NODELABEL(gpio1))};
    const struct device* radio_spi_dev_{DEVICE_DT_GET(DT_ALIAS(radio_spi))};
    const struct device* epd_spi_dev_{DEVICE_DT_GET(DT_ALIAS(epd_spi))};
    const struct device* gnss_uart_dev_{DEVICE_DT_GET(DT_ALIAS(gnss_uart))};
    // Both candidate barometer addresses are declared; whichever part is fitted
    // becomes ready, the other never does.
    const struct device* baro76_dev_{DEVICE_DT_GET(DT_NODELABEL(bme280_76))};
    const struct device* baro77_dev_{DEVICE_DT_GET(DT_NODELABEL(bme280_77))};
    struct pwm_dt_spec buzzer_{PWM_DT_SPEC_GET(DT_ALIAS(buzzer))};
    struct gpio_dt_spec vibro_{GPIO_DT_SPEC_GET(DT_ALIAS(vibro), gpios)};
    struct gpio_dt_spec button_{GPIO_DT_SPEC_GET(DT_ALIAS(button), gpios)};
    struct gpio_dt_spec radio_cs_{GPIO_DT_SPEC_GET_BY_IDX(DT_ALIAS(radio_spi), cs_gpios, 0)};
    struct gpio_dt_spec epd_cs_{GPIO_DT_SPEC_GET_BY_IDX(DT_ALIAS(epd_spi), cs_gpios, 0)};

    zephyr::Clock clock_{};
    Gpio gpio_{gpio0_, gpio1_};
    Spi radio_spi_{radio_spi_dev_, kSpiCfg, radio_cs_};
    Spi epd_spi_{epd_spi_dev_, kSpiCfg, epd_cs_};
    Uart gnss_uart_{gnss_uart_dev_};
    Annunciator annunciator_{buzzer_, vibro_};
    KvStore kv_{};
    Dfu dfu_{};
    zephyr::Link& link_{zephyr::link()};
    zephyr::Baro baro76_{baro76_dev_};
    zephyr::Baro baro77_{baro77_dev_};
    zephyr::Baro* baro_{nullptr};
    zephyr::Pps pps_{};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
