// devices/soc/zephyr/zephyr_dfu.h — hal::Dfu over MCUboot. This is a headline
// win of the migration: signed, roll-back-protected A/B firmware update, vs the
// legacy Adafruit serial DFU. core/ decides WHEN; this decides HOW.
#ifndef SKYBLIP_DEVICES_SOC_ZEPHYR_ZEPHYR_DFU_H
#define SKYBLIP_DEVICES_SOC_ZEPHYR_ZEPHYR_DFU_H
#if defined(__ZEPHYR__)

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

#include "hal/dfu.h"

namespace skyblip::soc::zephyr {

// A staged image (delivered over BLE SMP / img_mgmt into the secondary slot) is
// marked for a one-shot upgrade; MCUboot swaps it in on reboot and reverts if
// the new image fails to confirm. trigger() is the "go" from core/.
class ZephyrDfu : public hal::Dfu {
   public:
    void trigger() override {
        boot_request_upgrade(BOOT_UPGRADE_TEST);  // test = auto-revert if unconfirmed
        sys_reboot(SYS_REBOOT_WARM);
    }
};

}  // namespace skyblip::soc::zephyr
#endif  // __ZEPHYR__
#endif
