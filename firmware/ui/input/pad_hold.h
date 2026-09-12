#ifndef SKYBLIP_UI_INPUT_PAD_HOLD_H
#define SKYBLIP_UI_INPUT_PAD_HOLD_H

#include <cstdint>

#include "core/power/shutdown.h"

namespace skyblip::ui {

class PadHold {
   public:
    static constexpr uint32_t kHoldMs = power::kLongPressMs;

    bool update(bool pad_down, bool button_down, uint32_t now_ms) {
        if (!pad_down) {
            holding_ = false;
            spent_ = false;
            return false;
        }
        // INFO: fc 12sep26 a hold the button joins is a stow (core/power/shutdown.h), not a way in
        if (button_down) {
            holding_ = false;
            spent_ = true;
            return false;
        }
        if (spent_) return false;
        if (!holding_) {
            holding_ = true;
            since_ms_ = now_ms;
            return false;
        }
        if (now_ms - since_ms_ < kHoldMs) return false;
        spent_ = true;
        return true;
    }

    bool holding() const { return holding_; }

   private:
    uint32_t since_ms_{0};
    bool holding_{false};
    bool spent_{false};
};

}  // namespace skyblip::ui

#endif
