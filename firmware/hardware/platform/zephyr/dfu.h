#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_DFU_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_DFU_H
#if defined(__ZEPHYR__)

#include <zephyr/devicetree.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/retention/retention.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>

#if defined(CONFIG_SOC_FAMILY_NORDIC_NRF)
#include <hal/nrf_wdt.h>
#endif

#include "hal/dfu.h"

namespace skyblip::platform::zephyr {

// THE TWO MAGICS, and they are a pair. Both are one byte at offset 0 of the
// retention area declared on &gpregret1 in the board devicetree, which is the
// GPREGRET the factory Adafruit bootloader reads on every boot
// (Adafruit_nRF52_Bootloader src/main.c:106-110, check_dfu_mode at :241-270).
// Nothing else in this firmware writes that byte, and 0xA8 / 0xB1 - the
// bootloader's own OTA magics, which would hand control to its unsigned BLE
// update path - are written nowhere in the tree.
//
// 0x57 is deliberate recovery: come up as USB mass storage instead of
// chain-loading MCUboot. It is the SOFTWARE route into the factory bootloader,
// for a device whose reset button is awkward or enclosed; the other route is a
// double-click on RESET within 0.5 s, which is the bootloader's own and works
// whatever state this firmware is in.
constexpr uint8_t kUf2MassStorageMagic = 0x57;

// 0x6d is the opposite request: skip DFU entirely on the next boot. Written on
// the way to SYSTEM OFF (hardware/platform/zephyr/system_power.h), which is what
// SoftRF does before sleeping (src/platform/nRF52.cpp:3213-3226, magic at
// nRF52.h:128).
//
// WHAT IT BUYS. dfu_skip is tested before anything else in check_dfu_mode and
// returns immediately, so it wins over the bootloader's double-reset detector -
// and that detector reads a word of plain RAM at 0x20007F7C (DFU_DBL_RESET_MEM),
// whose contents through SYSTEM OFF are not defined. Without the magic, the first
// reset after a shutdown enters USB DFU if that word happens to hold 0x5A1AD5 and
// the reset pin was the cause. That is the "at the factory bootloader's
// discretion" this closes.
//
// WHAT IT COSTS, and §3 R1 has to keep working exactly as documented, so it is
// written down rather than discovered: taking the skip path also skips the 500 ms
// double-reset window on that ONE boot, and leaves DFU_DBL_RESET_MEM untouched
// instead of arming it. So a double-click of RESET performed on the very first
// boot after a deliberate power-off does not arm, and the pair after it does -
// the bootloader cleared the magic on that boot, so every later reset is the
// documented behaviour. R1's actual job is unaffected: it is the escape hatch for
// a device whose application is broken, and a broken application never reached a
// shutdown, so its GPREGRET is 0 and the first double-click works.
constexpr uint8_t kSkipBootloaderMagic = 0x6d;

// One writer for both. False when the board declares no retention area or the
// device is not ready: the caller decides what that means, and neither caller
// treats it as fatal - a board with no UF2 bootloader has nothing to ask.
inline bool write_boot_magic(uint8_t magic) {
#if DT_NODE_EXISTS(DT_NODELABEL(uf2_boot_retention))
    const struct device* retention = DEVICE_DT_GET(DT_NODELABEL(uf2_boot_retention));
    if (!device_is_ready(retention)) return false;
    return retention_write(retention, 0, &magic, sizeof(magic)) == 0;
#else
    (void)magic;
    return false;
#endif
}

// INFO: fc 07sep26 the one predicate every SMP write is gated on; products/ wires it to core/
using DfuGate = bool (*)();
void set_dfu_gate(DfuGate gate);

class Dfu : public hal::Dfu {
   public:
    void trigger() override {
        boot_request_upgrade(BOOT_UPGRADE_TEST);
        sys_reboot(SYS_REBOOT_WARM);
    }

    bool confirm() override { return boot_write_img_confirmed() == 0; }
    bool confirmed() override { return boot_is_img_confirmed(); }

    bool running_version(hal::ImageVersion& out) override {
        return read_version(FIXED_PARTITION_ID(slot0_partition), out);
    }
    bool staged_version(hal::ImageVersion& out) override {
        return read_version(FIXED_PARTITION_ID(slot1_partition), out);
    }

    // INFO: fc 04sep26 the WDT survives a soft reset, not SYSTEM OFF; it would cut the UF2 session
    hal::RecoveryPath enter_recovery() override {
        if (watchdog_running()) {
            recovery_armed_ = true;
            return hal::RecoveryPath::PowerOffToFinish;
        }
        write_boot_magic(kUf2MassStorageMagic);
        sys_reboot(SYS_REBOOT_WARM);
        return hal::RecoveryPath::Rebooted;
    }

    static uint8_t boot_magic_for_system_off() {
        return recovery_armed_ ? kUf2MassStorageMagic : kSkipBootloaderMagic;
    }

   private:
    static bool read_version(uint8_t area_id, hal::ImageVersion& out) {
        mcuboot_img_header header{};
        if (boot_read_bank_header(area_id, &header, sizeof(header)) != 0) return false;
        if (header.mcuboot_version != 1) return false;
        out.major = header.h.v1.sem_ver.major;
        out.minor = header.h.v1.sem_ver.minor;
        out.revision = header.h.v1.sem_ver.revision;
        out.build = header.h.v1.sem_ver.build_num;
        return true;
    }

    static bool watchdog_running() {
#if defined(CONFIG_SOC_FAMILY_NORDIC_NRF) && defined(NRF_WDT)
        return nrf_wdt_started_check(NRF_WDT);
#else
        return false;
#endif
    }

    inline static bool recovery_armed_ = false;
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
