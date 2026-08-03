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
