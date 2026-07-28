#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt_defines.h>

#include "hardware/platform/zephyr/dfu.h"

namespace skyblip::platform::zephyr {
namespace {

DfuGate g_gate = nullptr;

// Fail closed. An unregistered gate means the application has not finished
// wiring itself up, which is not a state in which to accept a firmware write.
bool allowed() { return g_gate != nullptr && g_gate(); }

mgmt_cb_return on_mgmt_event(uint32_t, mgmt_cb_return, int32_t* rc, uint16_t*, bool* abort_more,
                             void*, size_t) {
    if (allowed()) {
        return MGMT_CB_OK;
    }
    *rc = MGMT_ERR_EACCESSDENIED;
    *abort_more = true;
    return MGMT_CB_ERROR_RC;
}

// Zero-initialised and filled in at registration rather than aggregate-
// initialised: mgmt_callback's first member is the intrusive list node, which
// belongs to the subsystem, not to us.
mgmt_callback g_upload_callback{};
mgmt_callback g_reset_callback{};

}  // namespace

void set_dfu_gate(DfuGate gate) {
    g_gate = gate;

    static bool registered = false;
    if (registered) return;

    g_upload_callback.callback = on_mgmt_event;
    g_upload_callback.event_id = MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK;
    mgmt_callback_register(&g_upload_callback);

    g_reset_callback.callback = on_mgmt_event;
    g_reset_callback.event_id = MGMT_EVT_OP_OS_MGMT_RESET;
    mgmt_callback_register(&g_reset_callback);

    registered = true;
}

}  // namespace skyblip::platform::zephyr
