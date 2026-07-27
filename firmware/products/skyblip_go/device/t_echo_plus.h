// products/skyblip_go/device/t_echo_plus.h — the REAL T-Echo Plus, assembled.
//
// This is one half of a mirrored pair. Its twin is
// products/skyblip_go/simulator/t_echo_plus.h, which assembles the same board
// out of devices/models instead of devices/soc/zephyr. Diff the two and the
// difference IS the list of what the simulator virtualises — nothing else.
//
// Assembly only: adapters, pin bindings and bring-up order. main.cpp drives it.
#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_DEVICE_T_ECHO_PLUS_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_DEVICE_T_ECHO_PLUS_H

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

#include "devices/boards/t_echo_plus/pins.h"
#include "devices/drivers/l76k.h"
#include "devices/drivers/ssd1681.h"
#include "devices/drivers/sx1262.h"
#include "devices/soc/zephyr/zephyr_annunciator.h"
#include "devices/soc/zephyr/zephyr_ble.h"
#include "devices/soc/zephyr/zephyr_clock.h"
#include "devices/soc/zephyr/zephyr_dfu.h"
#include "devices/soc/zephyr/zephyr_io.h"
#include "devices/soc/zephyr/zephyr_kvstore.h"
#include "products/skyblip_go/app.h"

namespace skyblip::go::device {

namespace pins = skyblip::board::t_echo_plus;
namespace sz = skyblip::soc::zephyr;

inline const struct device* const kGpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
inline const struct device* const kGpio1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));
inline const struct device* const kRadioSpi = DEVICE_DT_GET(DT_ALIAS(radio_spi));
inline const struct device* const kEpdSpi = DEVICE_DT_GET(DT_ALIAS(epd_spi));
inline const struct device* const kGnssUart = DEVICE_DT_GET(DT_ALIAS(gnss_uart));

inline const struct pwm_dt_spec kBuzzer = PWM_DT_SPEC_GET(DT_ALIAS(buzzer));

inline const struct spi_config kSpiCfg = {
    .frequency = 8000000U,
    .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER,
    .slave = 0,
    .cs = {},  // manual CS (port==NULL): the drivers drive it via io::Spi::select()
};

// SoC unique id → 24-bit default ADS-L address (portable via hwinfo).
inline uint32_t chip_addr() {
    uint8_t id[8] = {0};
    ssize_t n = hwinfo_get_device_id(id, sizeof(id));
    uint32_t a = 0;
    for (ssize_t i = 0; i < 3 && i < n; i++) a = (a << 8) | id[i];
    return a & 0x00FFFFFFu;
}

struct TEchoPlus {
    sz::ZephyrClock clock{};
    sz::ZephyrGpio gpio{kGpio0, kGpio1};

    // Radio on its shared SPI bus (SS P0.24).
    sz::ZephyrSpi radio_spi{kRadioSpi, kSpiCfg, kGpio0, pins::kRadioSs & 31};
    drivers::Sx1262 radio{radio_spi, gpio, pins::kRadioBusy, pins::kRadioRst, pins::kRadioDio1};

    // E-paper on its own SPI bus (SS P0.30). Optional — nullptr if not ready.
    sz::ZephyrSpi epd_spi{kEpdSpi, kSpiCfg, kGpio0, pins::kEpdSs & 31};
    drivers::Ssd1681 epd{epd_spi, gpio, pins::kEpdDc, pins::kEpdRst, pins::kEpdBusy};

    // Buzzer + vibration motor: the alarm's ONLY output on this board.
    sz::ZephyrAnnunciator annunciator{kBuzzer, gpio, pins::kVibro};

    sz::ZephyrKvStore kv{};
    sz::ZephyrDfu dfu{};
    sz::BleLink& link{sz::ble_link()};
    sz::ZephyrUart gnss_uart{kGnssUart};
    drivers::L76k gnss{gnss_uart};

    bool have_epd{false};
    bool have_gnss{false};
    bool have_link{false};

    // Bring the peripherals up. Constructing the adapters above touches no
    // hardware, so the TCXO settling wait here still lands before the first SPI
    // transaction to the radio, which App::setup() issues.
    Status begin() {
        if (!device_is_ready(kGpio0) || !device_is_ready(kGpio1) || !device_is_ready(kRadioSpi))
            return Status::Down;

        wait_for_radio_supply();

        have_epd = device_is_ready(kEpdSpi);
        if (have_epd) epd.begin();

        kv.begin();
        annunciator.begin();
        have_link = link.begin() == Status::Ok;
        have_gnss = device_is_ready(kGnssUart);
        return Status::Ok;
    }

    go::Ports ports() {
        go::Ports p{clock, link, radio};
        p.display = have_epd ? &epd : nullptr;
        p.kv = &kv;
        p.annunciator = &annunciator;
        p.dfu = &dfu;
        p.device_addr = chip_addr();
        return p;
    }

   private:
    // The gated rails themselves are raised much earlier, by
    // boards/lilygo/t_echo_plus/board.c, because MCUboot needs the external
    // flash powered before the application exists. By the time this runs they
    // are up; what is still owed is the SX1262 TCXO settling time.
    static void wait_for_radio_supply() { k_msleep(50); }
};

}  // namespace skyblip::go::device

#endif
