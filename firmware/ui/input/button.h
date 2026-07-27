// ui/input/button.h — debounced button, as pure logic.
//
// A mechanical contact bounces for a few milliseconds on both edges, so a raw
// GPIO poll turns one press into several. This takes sampled levels in and emits
// ONE press per press. No framework, no clock of its own: the shell samples the
// pin and passes the level plus the time, which is what makes it host-testable
// and what lets the simulator inject presses through the same path.
#ifndef SKYBLIP_UI_INPUT_BUTTON_H
#define SKYBLIP_UI_INPUT_BUTTON_H

#include <cstdint>

namespace skyblip::ui {

class Button {
   public:
    // A contact settles well inside this; a human cannot press twice within it.
    static constexpr uint32_t kDebounceMs = 30;

    // Feed one sample. Returns true exactly once per debounced press, on the
    // edge where the level has been stable for kDebounceMs.
    bool update(bool down, uint32_t now_ms) {
        if (down != candidate_) {
            candidate_ = down;
            since_ms_ = now_ms;
            return false;
        }
        if (down == stable_) return false;
        if (now_ms - since_ms_ < kDebounceMs) return false;
        stable_ = down;
        return down;  // report the press edge, not the release
    }

    bool down() const { return stable_; }

   private:
    bool candidate_{false};
    bool stable_{false};
    uint32_t since_ms_{0};
};

}  // namespace skyblip::ui

#endif
