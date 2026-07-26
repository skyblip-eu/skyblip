// devices/soc/zephyr/zephyr_ble.h — the companion-link seam (§6) on Zephyr BT.
// The ONLY place GATT lives. Exposes a hal::Link the App sends through, plus a
// drain for inbound config frames. The GATT tables are file-static in ble.cpp
// (BT_GATT_SERVICE_DEFINE), so the link is a singleton.
#ifndef SKYBLIP_DEVICES_SOC_ZEPHYR_ZEPHYR_BLE_H
#define SKYBLIP_DEVICES_SOC_ZEPHYR_ZEPHYR_BLE_H
#if defined(__ZEPHYR__)

#include "core/messages/messages.h"
#include "hal/link.h"

namespace skyblip::soc::zephyr {

class BleLink : public hal::Link {
   public:
    Status begin();  // bt_enable() + start connectable advertising
    Status send(messages::Endpoint ep, ConstByteSpan bytes) override;

    // Non-blocking: pop one queued inbound frame (config writes). The shell
    // drains this into App::on_link_rx(). Returns false when empty.
    bool pop_rx(messages::RxFrame& out);
};

BleLink& ble_link();

}  // namespace skyblip::soc::zephyr
#endif  // __ZEPHYR__
#endif
