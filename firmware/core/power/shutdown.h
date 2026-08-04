// core/power/shutdown.h: the order in which a device is allowed to go dark.
//
// Three things ask for it - a long press, the cell reaching its cutoff, and the
// companion link - and all three take the same road: park the radio and the
// panel, wait for the button to actually be up, then drop the rails. The shell
// performs each step; what may happen when is decided here, on the host.
#ifndef SKYBLIP_CORE_POWER_SHUTDOWN_H
#define SKYBLIP_CORE_POWER_SHUTDOWN_H

#include <cstdint>

namespace skyblip::power {

enum class ShutdownReason : uint8_t { None, LongPress, LowBattery, LinkRequest };
enum class ShutdownPhase : uint8_t { Running, Parking, AwaitRelease, Off };

const char* to_string(ShutdownReason reason);

// What dropping the rails is made of. Every one of these is harmless on its
// own; four of them are wrong in the wrong place, so the ORDER below is the
// content of this module and the thing the tests hold.
//
// INFO: fc 06aug26 THE SLEEP CURRENT THIS SEQUENCE ACTUALLY LEAVES HAS NEVER BEEN
// MEASURED, and no test here can measure it: what these cases prove is the order,
// not the microamps. Before this existed, SYSTEM OFF left the radio, the receiver,
// the panel, the barometer and the external flash powered, so an 850 mAh pack did
// not survive a night in a flight bag while every visible sign said the device was
// off. The number on the bench is what decides whether the product needs a
// hardware switch, and it is the one measurement nothing in this repository can
// stand in for.
enum class PowerDownStep : uint8_t {
    // An armed dwell keeps the receiver and the PA alive, and the executor
    // re-arms it as long as it is running. Nothing downstream is true until the
    // part is in SetSleep.
    RadioSleep,
    // Deep Power-Down, 0xB9. It is a command over SPI, so the part has to still
    // have a supply to hear it: after the rail, it is a no-op.
    ExternalFlashDeepPowerDown,
    // CS, WP# and HOLD# are the lines that command travelled on.
    ExternalFlashLinesReleased,
    // L76K: the wake pin low, then reset asserted. The receiver is the largest
    // steady draw on the board with a fix.
    GnssBackupOff,
    GnssResetAsserted,
    // SX1262 NRESET. After RadioSleep, because a reset pulse on a running radio
    // loses the retained configuration for nothing.
    RadioResetAsserted,
    // The rails collapse last, and only once every part behind them has been
    // told and every line into them is at zero.
    PeripheralRailOff,
    AuxRailOff,
    // INFO: hk 03aug26 SoftRF-moshe-braner src/platform/nRF52.cpp:2055-2080
    // drives each enable pin OUTPUT LOW before switching it to input, because
    // that "clears the output latch AND actively shuts off the external
    // regulator before we release the pin". The release is therefore its own
    // step and it comes after the rails are already down.
    DrivenPinsReleased,
    // Level-sensed, so it is armed only once the button is up: see
    // kReleaseSettleMs. Last, because arming it earlier races the rails.
    WakePinArmed,
};

constexpr int kPowerDownStepCount = 10;

inline constexpr PowerDownStep kPowerDownOrder[kPowerDownStepCount] = {
    PowerDownStep::RadioSleep,
    PowerDownStep::ExternalFlashDeepPowerDown,
    PowerDownStep::ExternalFlashLinesReleased,
    PowerDownStep::GnssBackupOff,
    PowerDownStep::GnssResetAsserted,
    PowerDownStep::RadioResetAsserted,
    PowerDownStep::PeripheralRailOff,
    PowerDownStep::AuxRailOff,
    PowerDownStep::DrivenPinsReleased,
    PowerDownStep::WakePinArmed,
};

// Where a step sits in the order, so an invariant can be written as a
// comparison instead of a comment.
constexpr int step_order(PowerDownStep step) {
    for (int i = 0; i < kPowerDownStepCount; i++)
        if (kPowerDownOrder[i] == step) return i;
    return -1;
}

// The enum is closed and the table has one entry per value, so no duplicates is
// the same statement as every step present.
constexpr bool order_lists_each_step_once() {
    for (int i = 0; i < kPowerDownStepCount; i++)
        for (int j = i + 1; j < kPowerDownStepCount; j++)
            if (kPowerDownOrder[i] == kPowerDownOrder[j]) return false;
    return true;
}

static_assert(static_cast<int>(PowerDownStep::WakePinArmed) == kPowerDownStepCount - 1,
              "a step was added to the enum and not to the order");
static_assert(order_lists_each_step_once(), "a step listed twice is a step performed twice");
static_assert(step_order(PowerDownStep::WakePinArmed) == kPowerDownStepCount - 1,
              "the wake pin is armed last, after the rails it would otherwise race");
static_assert(step_order(PowerDownStep::ExternalFlashDeepPowerDown) <
                  step_order(PowerDownStep::PeripheralRailOff),
              "0xB9 sent to a flash whose rail is already down is a no-op");
static_assert(step_order(PowerDownStep::ExternalFlashDeepPowerDown) <
                  step_order(PowerDownStep::ExternalFlashLinesReleased),
              "the flash cannot be commanded over lines that have been released");
static_assert(step_order(PowerDownStep::RadioSleep) < step_order(PowerDownStep::RadioResetAsserted),
              "the radio is put to sleep before its reset line is touched");
static_assert(step_order(PowerDownStep::GnssResetAsserted) <
                  step_order(PowerDownStep::PeripheralRailOff),
              "the GNSS is told to stop before its supply is taken away");
static_assert(step_order(PowerDownStep::PeripheralRailOff) <
                      step_order(PowerDownStep::DrivenPinsReleased) &&
                  step_order(PowerDownStep::AuxRailOff) <
                      step_order(PowerDownStep::DrivenPinsReleased),
              "a pin is released only once there is no rail left to back-feed");

// INFO: hk 03aug26 the 20 ms MB spends between driving the enable pins low and
// releasing them (nRF52.cpp:2075). It is the rail's discharge, not the pin's.
constexpr uint32_t kRailSettleMs = 20;

const char* to_string(PowerDownStep step);

// What a board can actually do, one step at a time. The board decides how; this
// module decides when, and that is the whole division.
class PowerDownSink {
   public:
    virtual ~PowerDownSink() = default;
    virtual void perform(PowerDownStep step) = 0;
};

// Walks kPowerDownOrder once, in order. The caller enters SYSTEM OFF after it
// returns.
void power_down(PowerDownSink& sink);

// Long enough that it cannot be the page press, short enough to do with gloves
// on. A short press pages; this is the only other thing the one button does.
constexpr uint32_t kLongPressMs = 2000;

// The panel is parked with a full refresh and the SSD1681 clocks one out in
// about 2.5 s. Dropping the rails before it finishes leaves half an image on the
// glass, which is what the device then wears until someone turns it back on.
constexpr uint32_t kParkMs = 3000;

// INFO: hk 02aug26 nRF52 SENSE is a level detect, not an edge, so arming the
// wake pin while the button is still down wakes the device the instant SYSTEM
// OFF latches. SoftRF spins on the pin and waits 100 ms before arming it
// (src/platform/nRF52.cpp:3199-3203).
constexpr uint32_t kReleaseSettleMs = 100;

class ShutdownSequencer {
   public:
    // One sample of the world per service step. The button level is the raw
    // debounced level, not the page-press edge: a hold produces no edges.
    void tick(uint32_t now_ms, bool button_down);

    // The other two entries. The first reason to arrive is the one reported;
    // nothing cancels a shutdown once it has started.
    void request(ShutdownReason reason, uint32_t now_ms);

    ShutdownPhase phase() const { return phase_; }
    ShutdownReason reason() const { return reason_; }
    bool going_down() const { return phase_ != ShutdownPhase::Running; }
    bool ready_to_power_off() const { return phase_ == ShutdownPhase::Off; }

    // How long the button has been down, for a screen that wants to show the
    // hold filling up. Zero when it is not down or the hold is not armed yet.
    uint32_t held_ms(uint32_t now_ms) const;

   private:
    void enter(ShutdownPhase phase, uint32_t now_ms);

    ShutdownPhase phase_{ShutdownPhase::Running};
    ShutdownReason reason_{ShutdownReason::None};
    uint32_t since_ms_{0};
    uint32_t hold_since_ms_{0};
    uint32_t released_at_ms_{0};
    bool holding_{false};
    bool released_{false};
    // A press is what wakes the device from SYSTEM OFF, so the very first thing
    // the sequencer sees after a wake is a button that is already down. Counting
    // that as a hold powers the device off again before the panel has drawn.
    bool hold_armed_{false};
};

}  // namespace skyblip::power

#endif
