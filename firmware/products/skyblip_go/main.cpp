#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "hardware/platform/zephyr/platform.h"
#include "products/skyblip_go/product.h"
#include "runtime/tasks.h"

LOG_MODULE_REGISTER(skyblip, LOG_LEVEL_INF);

using namespace skyblip;

namespace {

using Go = go::Product<platform::zephyr::Platform>;

platform::zephyr::Platform g_platform;
Go g_product{g_platform};

// Capture-less so it converts to the plain function pointer the hook takes.
bool dfu_gate() { return g_product.config().config().upload_allowed(); }

}  // namespace

int main(void) {
    // Until this runs, the MCUmgr hooks fail closed and refuse every upload.
    platform::zephyr::set_dfu_gate(dfu_gate);

    const Status started = g_product.setup();
    LOG_INF("reset reason: %s", power::to_string(g_product.reset_reason()));
    if (started == Status::Ok) {
        LOG_INF("skyBlip up: capabilities=%u degraded=%u",
                static_cast<unsigned>(g_product.capabilities()),
                static_cast<unsigned>(g_product.degraded()));
    } else {
        // The self-test page is already on the glass and names the part that
        // did not answer. Staying up with it beats returning from main.
        LOG_ERR("self test %s: missing=%u", to_string(started),
                static_cast<unsigned>(hal::missing(g_product.capabilities(), go::kRequired)));
    }

    // Armed last, after every part is up: bring-up is slower than any
    // steady-state pass, and on the nRF52 a watchdog that has started can never
    // be stopped again.
    if (g_platform.watchdog().arm(runtime::kHardwareWatchdogMs) != Status::Ok)
        LOG_ERR("watchdog: not armed, the loop is unsupervised");

    bool reported_stall = false;
    for (;;) {
        const uint32_t now_ms = static_cast<uint32_t>(k_uptime_get());
        g_product.step(now_ms);

        if (g_product.may_feed_watchdog(now_ms)) {
            g_platform.watchdog().feed();
        } else if (!reported_stall) {
            reported_stall = true;
            const int stalled = g_product.stalled_service(now_ms);
            LOG_ERR("watchdog: service '%s' stopped reporting progress; letting it bite",
                    g_product.service_name(stalled));
        }

        if (g_product.ready_to_power_off()) {
            LOG_INF("power off: %s", power::to_string(g_product.shutdown().reason()));
            g_platform.system_power().system_off();
        }

        k_sleep(K_MSEC(runtime::kServiceStepMs));
    }
    return 0;
}
