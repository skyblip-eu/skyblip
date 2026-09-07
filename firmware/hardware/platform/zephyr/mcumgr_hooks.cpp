#include <zephyr/mgmt/mcumgr/grp/img_mgmt/img_mgmt.h>
#include <zephyr/mgmt/mcumgr/grp/os_mgmt/os_mgmt.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt_defines.h>

#include "core/dfu/smp_policy.h"
#include "hardware/platform/zephyr/dfu.h"

namespace skyblip::platform::zephyr {
namespace {

static_assert(static_cast<uint8_t>(dfu::SmpOp::Read) == MGMT_OP_READ);
static_assert(static_cast<uint8_t>(dfu::SmpOp::Write) == MGMT_OP_WRITE);
static_assert(static_cast<uint16_t>(dfu::SmpGroup::Os) == MGMT_GROUP_ID_OS);
static_assert(static_cast<uint16_t>(dfu::SmpGroup::Image) == MGMT_GROUP_ID_IMAGE);
static_assert(dfu::kSmpOsEcho == OS_MGMT_ID_ECHO);
static_assert(dfu::kSmpOsReset == OS_MGMT_ID_RESET);
static_assert(dfu::kSmpImageState == IMG_MGMT_ID_STATE);
static_assert(dfu::kSmpImageUpload == IMG_MGMT_ID_UPLOAD);
static_assert(dfu::kSmpImageErase == IMG_MGMT_ID_ERASE);

DfuGate g_gate = nullptr;

bool upload_allowed() { return g_gate != nullptr && g_gate(); }

mgmt_cb_return refuse(int32_t* rc, bool* abort_more) {
    *rc = MGMT_ERR_EACCESSDENIED;
    *abort_more = true;
    return MGMT_CB_ERROR_RC;
}

// INFO: fc 07sep26 img_mgmt state-write and erase hooks only notify, so every write is gated here
mgmt_cb_return on_command(uint32_t event, mgmt_cb_return, int32_t* rc, uint16_t*, bool* abort_more,
                          void* data, size_t data_size) {
    if (event != MGMT_EVT_OP_CMD_RECV || data == nullptr ||
        data_size != sizeof(mgmt_evt_op_cmd_arg))
        return refuse(rc, abort_more);
    const auto* received = static_cast<const mgmt_evt_op_cmd_arg*>(data);
    const dfu::SmpCommand command{received->group, received->id, received->op};
    return dfu::smp_permitted(command, upload_allowed()) ? MGMT_CB_OK : refuse(rc, abort_more);
}

mgmt_callback g_command_callback{};

}  // namespace

void set_dfu_gate(DfuGate gate) {
    g_gate = gate;

    static bool registered = false;
    if (registered) return;

    g_command_callback.callback = on_command;
    g_command_callback.event_id = MGMT_EVT_OP_CMD_RECV;
    mgmt_callback_register(&g_command_callback);

    registered = true;
}

}  // namespace skyblip::platform::zephyr
