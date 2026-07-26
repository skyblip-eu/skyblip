// products/skyblip/main.cpp — the skyBlip Go composition root (Zephyr).
// §8: construct the SoC adapters from devicetree, then drive the shared,
// framework-agnostic App (products/skyblip/app.h). All logic lives in App;
// this file is the shell: adapters + power sequencing + the drain/step loop.
//
// Concurrency: App is single-threaded and owns all logic; it runs in this (main)
// thread. Producers only ENQUEUE — the BT stack fills the BLE rx fifo from its
// own thread. Nothing else touches App, so no locks around App state.
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "devices/boards/techo/pins.h"
#include "devices/drivers/ssd1681.h"
#include "devices/drivers/sx1262.h"
#include "devices/soc/zephyr/zephyr_ble.h"
#include "devices/soc/zephyr/zephyr_clock.h"
#include "devices/soc/zephyr/zephyr_dfu.h"
#include "devices/soc/zephyr/zephyr_io.h"
#include "devices/soc/zephyr/zephyr_kvstore.h"
#include "products/skyblip/app.h"

LOG_MODULE_REGISTER(skyblip, LOG_LEVEL_INF);

using namespace skyblip;
namespace tp = skyblip::board::techo;
namespace sz = skyblip::soc::zephyr;

namespace {

const struct device* const kGpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
const struct device* const kGpio1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));
const struct device* const kRadioSpi = DEVICE_DT_GET(DT_ALIAS(radio_spi));
const struct device* const kEpdSpi = DEVICE_DT_GET(DT_ALIAS(epd_spi));
const struct device* const kGnssUart = DEVICE_DT_GET(DT_ALIAS(gnss_uart));

const struct spi_config kSpiCfg = {
    .frequency = 8000000U,
    .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER,
    .slave = 0,
    .cs = {},  // manual CS (port==NULL): the drivers drive it via io::Spi::select()
};

// SoC unique id → 24-bit default ADS-L address (portable via hwinfo).
uint32_t chip_addr() {
    uint8_t id[8] = {0};
    ssize_t n = hwinfo_get_device_id(id, sizeof(id));
    uint32_t a = 0;
    for (ssize_t i = 0; i < 3 && i < n; i++) a = (a << 8) | id[i];
    return a & 0x00FFFFFFu;
}

// The T-Echo power-gates the SX1262 and the e-paper behind IO_PWR / 3V3_PWR.
// These MUST go high (and settle) before any SPI to the radio or panel, or the
// bus reads back nothing. This is the #1 hardware bring-up gotcha.
void power_up_rails(io::Gpio& gpio) {
    gpio.mode_output(tp::kIoPwr);
    gpio.set(tp::kIoPwr, true);
    gpio.mode_output(tp::k3v3Pwr);
    gpio.set(tp::k3v3Pwr, true);
    k_msleep(50);  // let the rails and the SX1262 TCXO settle
}

}  // namespace

int main(void) {
    if (!device_is_ready(kGpio0) || !device_is_ready(kGpio1) || !device_is_ready(kRadioSpi)) {
        LOG_ERR("required devices not ready");
        return -1;
    }

    static sz::ZephyrClock clock;
    static sz::ZephyrGpio gpio(kGpio0, kGpio1);

    power_up_rails(gpio);

    // Radio on its shared SPI bus (SS P0.24).
    static sz::ZephyrSpi radio_spi(kRadioSpi, kSpiCfg, kGpio0, tp::kRadioSs & 31);
    static drivers::Sx1262 radio(radio_spi, gpio, tp::kRadioBusy, tp::kRadioRst, tp::kRadioDio1);

    // E-paper on its own SPI bus (SS P0.30). Optional — nullptr if not ready.
    static sz::ZephyrSpi epd_spi(kEpdSpi, kSpiCfg, kGpio0, tp::kEpdSs & 31);
    static drivers::Ssd1681 epd(epd_spi, gpio, tp::kEpdDc, tp::kEpdRst, tp::kEpdBusy);
    bool have_epd = device_is_ready(kEpdSpi);
    if (have_epd) epd.begin();

    static sz::ZephyrKvStore kv;
    kv.begin();

    static sz::ZephyrDfu dfu;
    static sz::BleLink& link = sz::ble_link();
    if (link.begin() != Status::Ok) LOG_WRN("BLE bring-up failed");

    static sz::ZephyrUart gnss(kGnssUart);

    static product::Ports ports{clock, link, radio};
    ports.display = have_epd ? &epd : nullptr;
    ports.kv = &kv;
    ports.dfu = &dfu;
    ports.gnss = device_is_ready(kGnssUart) ? &gnss : nullptr;
    ports.device_addr = chip_addr();

    static product::App app(ports);
    if (app.setup() != Status::Ok) LOG_ERR("app setup failed");
    LOG_INF("skyBlip up: addr=%06x epd=%d", ports.device_addr, have_epd);

    messages::RxFrame frame;
    for (;;) {
        while (link.pop_rx(frame)) app.on_link_rx(frame);
        app.step(clock.millis());
        k_sleep(K_MSEC(10));  // cooperative cadence; tighten for slot timing.
    }
    return 0;
}
