// devices/soc/zephyr/mcumgr_hooks.cpp — authorise MCUmgr image uploads and
// reboots.
//
// This is the control that replaces link encryption. CONFIG_BT_SMP is off so the
// browser can reach the SMP service over Web Bluetooth (encrypted GATT
// characteristics are unreliable there, notably on Windows), which means any
// peer in range can talk to the service. It cannot install anything: MCUboot
// checks an ECDSA-P256 signature. What it could otherwise do is fill the
// secondary slot, or reboot the device mid-flight. Both are refused here.
//
// The decision itself is not made in this file. core/comms/config.cpp owns it
// (positively-on-the-ground, airborne-latched, opened by a physical button
// press, closed on disconnect or timeout) and is covered by the host suite.
#include <zephyr/mgmt/mcumgr/grp/img_mgmt/img_mgmt_callbacks.h>
#include <zephyr/mgmt/mcumgr/grp/os_mgmt/os_mgmt_callbacks.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt_defines.h>

#include "devices/soc/zephyr/zephyr_dfu.h"

namespace skyblip::soc::zephyr {
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

}  // namespace skyblip::soc::zephyr
