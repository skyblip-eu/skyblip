// devices/soc/zephyr/zephyr_dfu.h — hal::Dfu over MCUboot, plus the hook the
// MCUmgr image-upload authorisation goes through.
//
// The bootloader chain is: factory Adafruit UF2 bootloader (0xF4000) →
// MCUboot (0x26000) → skyBlip (slot0). MCUboot verifies an ECDSA-P256 signature
// before it will boot an image, so the SMP transport does not have to be
// encrypted for the update to be secure — which is what allows the browser
// update page to exist at all.
#ifndef SKYBLIP_DEVICES_SOC_ZEPHYR_ZEPHYR_DFU_H
#define SKYBLIP_DEVICES_SOC_ZEPHYR_ZEPHYR_DFU_H
#if defined(__ZEPHYR__)

#include <zephyr/devicetree.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/retention/retention.h>
#include <zephyr/sys/reboot.h>

#include "hal/dfu.h"

namespace skyblip::soc::zephyr {

// Predicate the MCUmgr hooks consult before accepting an image chunk or a
// reboot. products/ registers one that defers to core/'s ConfigService, keeping
// the policy in host-tested code and this file as plumbing.
using DfuGate = bool (*)();
void set_dfu_gate(DfuGate gate);

class ZephyrDfu : public hal::Dfu {
   public:
    void trigger() override {
        boot_request_upgrade(BOOT_UPGRADE_TEST);
        sys_reboot(SYS_REBOOT_WARM);
    }

    void confirm() override { boot_write_img_confirmed(); }

    // The factory bootloader reads GPREGRET on every boot and enters USB mass
    // storage when it holds 0x57 (Adafruit_nRF52_Bootloader src/main.cpp:109
    // DFU_MAGIC_UF2_RESET). 0xA8 and 0xB1 are its OTA magics and are
    // deliberately never written anywhere in this firmware: they would hand
    // control to the bootloader's own unsigned BLE update path.
    void enter_recovery() override {
#if DT_NODE_EXISTS(DT_NODELABEL(uf2_boot_retention))
        static const uint8_t kUf2MassStorageMagic = 0x57;
        const struct device* retention = DEVICE_DT_GET(DT_NODELABEL(uf2_boot_retention));

        if (device_is_ready(retention)) {
            retention_write(retention, 0, &kUf2MassStorageMagic, sizeof(kUf2MassStorageMagic));
        }
#endif
        // Reboot regardless: on a board with no UF2 bootloader this is still the
        // most useful thing "recovery" can mean, and staying up pretending to
        // have acted would be worse.
        sys_reboot(SYS_REBOOT_WARM);
    }
};

}  // namespace skyblip::soc::zephyr
#endif  // __ZEPHYR__
#endif
