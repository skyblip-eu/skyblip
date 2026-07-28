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

    if (g_product.setup() != Status::Ok) {
        LOG_ERR("required capabilities missing: %u",
                static_cast<unsigned>(g_product.degraded()));
        return -1;
    }
    LOG_INF("skyBlip up: capabilities=%u", static_cast<unsigned>(g_product.capabilities()));

    for (;;) {
        g_product.step(static_cast<uint32_t>(k_uptime_get()));
        k_sleep(K_MSEC(runtime::kServiceStepMs));
    }
    return 0;
}
