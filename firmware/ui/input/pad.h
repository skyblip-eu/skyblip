#ifndef SKYBLIP_UI_INPUT_PAD_H
#define SKYBLIP_UI_INPUT_PAD_H

#include <cstdint>

#include "core/power/shutdown.h"

namespace skyblip::ui {

enum class PadEvent : uint8_t { None, Tap, Hold };

class Pad {
   public:
    static constexpr uint32_t kHoldMs = 1000;
    static constexpr uint32_t kSettleMs = 30;

    static_assert(kHoldMs < power::kLongPressMs,
                  "the pad's way home has to resolve before the hold that stows the device");

    PadEvent update(bool pad_down, bool button_down, uint32_t now_ms) {
        if (pad_down != candidate_) {
            candidate_ = pad_down;
            edge_ms_ = now_ms;
        }
        if (candidate_ != touching_ && settled(now_ms)) {
            touching_ = candidate_;
            if (!touching_) return released();
            touched_ms_ = edge_ms_;
            spent_ = button_down;
            return PadEvent::None;
        }
        if (!touching_ || !candidate_ || spent_) return PadEvent::None;
        // INFO: fc 17sep26 a touch the button joins is the stow (core/power/shutdown.h)
        if (button_down) {
            spent_ = true;
            return PadEvent::None;
        }
        if (now_ms - touched_ms_ < kHoldMs) return PadEvent::None;
        spent_ = true;
        return PadEvent::Hold;
    }

    bool touching() const { return touching_; }

   private:
    bool settled(uint32_t now_ms) const { return now_ms - edge_ms_ >= kSettleMs; }

    PadEvent released() {
        if (spent_) return PadEvent::None;
        spent_ = true;
        return touch_was_shorter_than_the_hold() ? PadEvent::Tap : PadEvent::Hold;
    }

    bool touch_was_shorter_than_the_hold() const { return edge_ms_ - touched_ms_ < kHoldMs; }

    uint32_t edge_ms_{0};
    uint32_t touched_ms_{0};
    bool candidate_{false};
    bool touching_{false};
    bool spent_{false};
};

}  // namespace skyblip::ui

#endif
