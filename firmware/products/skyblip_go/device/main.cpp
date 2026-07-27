// products/skyblip_go/device/main.cpp — the skyBlip Go entry point on silicon.
// §8: the board is assembled next door in t_echo_plus.h; this file only starts
// it, drives the shared App (products/skyblip_go/app.h), and owns the loop.
//
// Concurrency: App is single-threaded and owns all logic; it runs in this (main)
// thread. Producers only ENQUEUE — the BT stack fills the BLE rx fifo from its
// own thread. Nothing else touches App, so no locks around App state.
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "products/skyblip_go/device/t_echo_plus.h"

LOG_MODULE_REGISTER(skyblip, LOG_LEVEL_INF);

using namespace skyblip;

namespace {

go::App* g_app = nullptr;

// Capture-less so it converts to the plain function pointer the adapter takes.
bool dfu_gate() { return g_app != nullptr && g_app->config().upload_allowed(); }

// The BME280 in forced mode takes ~10 ms to convert, so sampling it every loop
// iteration would spend the whole 10 ms cadence waiting on I2C for no gain:
// 4 Hz is already twice the vertical-speed window.
constexpr uint32_t kBaroPeriodMs = 250;

}  // namespace

int main(void) {
    static go::device::TEchoPlus board;
    if (board.begin() != Status::Ok) {
        LOG_ERR("required devices not ready");
        return -1;
    }
    if (!board.have_link) LOG_WRN("BLE bring-up failed");

    static go::Ports ports = board.ports();
    static go::App app(ports);
    g_app = &app;
    // Until this runs, the MCUmgr hooks fail closed and refuse every upload.
    soc::zephyr::set_dfu_gate(dfu_gate);

    if (app.setup() != Status::Ok) LOG_ERR("app setup failed");
    LOG_INF("skyBlip up: addr=%06x epd=%d baro=%d", ports.device_addr, board.have_epd,
            board.have_baro);

    messages::RxFrame frame;
    uint32_t last_baro_ms = 0;
    for (;;) {
        while (board.link.pop_rx(frame)) app.on_link_rx(frame);
        if (board.have_gnss && board.gnss.poll()) app.on_gnss_fix(board.gnss.fix());
        const uint32_t now_ms = board.clock.millis();
        if (board.button_pressed(now_ms)) app.on_button();
        if (board.have_baro && now_ms - last_baro_ms >= kBaroPeriodMs) {
            last_baro_ms = now_ms;
            uint32_t pa = 0;
            if (board.baro.read_pressure_pa(pa)) app.on_baro(pa, now_ms);
        }
        app.step(now_ms);
        k_sleep(K_MSEC(10));  // cooperative cadence; tighten for slot timing.
    }
    return 0;
}
