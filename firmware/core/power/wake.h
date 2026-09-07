// core/power/wake.h: what the wake cause is allowed to make of the boot.
//
// The reset cause has been read, named and shown on the self-test page since
// pull request 14; nothing acted on it. This is the acting half, and it decides
// exactly one thing: whether this boot becomes a device or goes straight back to
// SYSTEM OFF. Everything here is a function of bits the platform reported, so the
// rule is host-tested and the adapter only reports and performs.
#ifndef SKYBLIP_CORE_POWER_WAKE_H
#define SKYBLIP_CORE_POWER_WAKE_H

#include <cstdint>

#include "core/power/cutoff.h"
#include "core/power/reset_reason.h"
#include "core/power/shutdown.h"

namespace skyblip::power {

// Run: bring the device up. SleepAgain: undo the wake - drop the rails and go
// back to SYSTEM OFF without painting the panel or starting the loop.
enum class BootPath : uint8_t { Run, SleepAgain };

const char* to_string(BootPath path);

struct BootCell {
    uint16_t millivolts{0};
    bool valid{false};
    bool external_power{false};
};

// INFO: fc 07sep26 meshcore NRF52Board.cpp:98-126 boot-locks at 3300 mV, against over-discharge
constexpr uint16_t kBootLockoutMv = 3400;

static_assert(kCutoffMv < kBootLockoutMv,
              "a cell recovers once the load stops: the shutdown would be undone by the next "
              "press");
static_assert(kBootLockoutMv < kLowWarnMv,
              "a boot refused before the pilot has been warned is a device that reads as dead");
static_assert(kImplausibleFloorMv < kCutoffMv,
              "an unpopulated divider must fall through the lockout, not into it");

// Same shape as the two references that fly on this hardware: nrf52-ogn-tracker
// src/main.cpp:517-532 (T_Echo_StayOffOnChargerWake) and SoftRF-lyusupov
// src/platform/nRF52.cpp:944-951.
//
// INFO: fc 05aug26 CHARGE MODE WAS CONSIDERED AND REFUSED, for now.
// SoftRF-moshe-braner offers one instead of sleeping (src/platform/nRF52.cpp:
// 2512-2560): beep the battery level out of the buzzer, then sleep. It is a
// better answer to the question a pilot actually asks - "is it charging?" - and
// on this hardware the e-paper holds its last image with the rails down, so an
// off device and a charging device look identical (item F of the same spec). It
// is not implemented here, and the reason is not cost:
//
//   1. The audible half would be the only alive-and-charging indicator on the
//      device, and item F is still open and asks for the state-to-indicator
//      mapping to be ONE table in core/, the charging case included. A buzzer
//      ladder invented in the wake path would be a second indicator vocabulary
//      that item F would then have to unpick.
//   2. What it would beep is not yet trustworthy. The gauge medians three
//      samples before it says anything (core/power/battery.h) and the per-unit
//      trim (item H) is a settings value, so an honest level is available after
//      the store is up and three samples in - which is most of a boot, on every
//      cable wiggle in a flight bag. The bag is the case this item exists for.
//   3. Refusing the boot is the whole of the P1 finding and it is complete on its
//      own. Charge mode is an addition to it, not an alternative: BootPath gains
//      a third value, the rule below is untouched, and the sleep path stays.
//
// So: sleep again, and when item F lands its table, add the announcement.
BootPath boot_path(ResetCause causes, bool button_down, const BootCell& cell);

ButtonWake button_wake_after_refusal(const BootCell& cell);

}  // namespace skyblip::power

#endif
