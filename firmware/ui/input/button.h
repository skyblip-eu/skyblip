// ui/input/button.h: debounced button, as pure logic.
//
// A mechanical contact bounces for a few milliseconds on both edges, so a raw
// GPIO poll turns one press into several. This takes sampled levels in and emits
// ONE press per press. No framework, no clock of its own: the shell samples the
// pin and passes the level plus the time, which is what makes it host-testable
// and what lets the simulator inject presses through the same path.
#ifndef SKYBLIP_UI_INPUT_BUTTON_H
#define SKYBLIP_UI_INPUT_BUTTON_H

#include <cstdint>

#include "core/power/shutdown.h"

namespace skyblip::ui {

class Button {
   public:
    // A contact settles well inside this. A human cannot press twice within it.
    static constexpr uint32_t kDebounceMs = 30;

    // INFO: fc 12sep26 SoftRF pages off kEventClicked, a release (lyusupov nRF52.cpp:4781-4795)
    bool update(bool down, uint32_t now_ms) {
        if (down != candidate_) {
            candidate_ = down;
            since_ms_ = now_ms;
            return false;
        }
        if (down == stable_) return false;
        if (now_ms - since_ms_ < kDebounceMs) return false;
        stable_ = down;
        if (down) {
            down_since_ms_ = since_ms_;
            return false;
        }
        return released_before_the_hold();
    }

    bool down() const { return stable_; }

   private:
    bool released_before_the_hold() const {
        return since_ms_ - down_since_ms_ < power::kLongPressMs;
    }

    bool candidate_{false};
    bool stable_{false};
    uint32_t since_ms_{0};
    uint32_t down_since_ms_{0};
};

}  // namespace skyblip::ui

#endif
