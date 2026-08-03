// ui/input/gesture.h: the gesture that authorises, as pure logic.
//
// This product ships with BLE pairing off, so there is no cryptographic proof
// that the phone asking for a firmware upload belongs to the pilot. Physical
// presence stands in for it, which makes the gesture the security boundary: it
// has to be something a thumb cannot produce by accident, and something that
// cannot be confused with the press that changes pages or with the hold that
// switches the device off.
//
// The one button already carries both of those. A double press is the third
// thing it can say, and it is the only one of the three that a pilot has to mean
// to make: two contacts inside a window a single deliberate press never spans.
//
// No framework, no clock of its own: the shell feeds debounced press edges and
// the time, which is what lets the simulator authorise through the same path.
#ifndef SKYBLIP_UI_INPUT_GESTURE_H
#define SKYBLIP_UI_INPUT_GESTURE_H

#include <cstdint>

namespace skyblip::ui {

enum class Gesture : uint8_t { None, Confirm, Cancel };

class ConfirmGesture {
   public:
    // INFO: cf 02aug26 Wider than a human double tap (~200 ms) and far narrower
    // than core/power's kLongPressMs, so the three things the button says stay
    // disjoint: one press pages or refuses, two inside this window authorise, a
    // hold past 2 s powers the device down.
    static constexpr uint32_t kDoublePressMs = 600;

    // INFO: cf 02aug26 Disarmed is the resting state and it authorises nothing.
    // A press only ever counts towards a confirmation while a prompt the pilot
    // can read is on the glass, so presses made before the prompt appeared - the
    // ones that were meant to change pages - can never be spent on it.
    void arm(uint32_t now_ms) {
        armed_ = true;
        pressed_ = false;
        first_ms_ = now_ms;
    }
    void disarm() {
        armed_ = false;
        pressed_ = false;
    }
    bool armed() const { return armed_; }
    bool pressed() const { return pressed_; }

    Gesture press(uint32_t now_ms) {
        if (!armed_) return Gesture::None;
        if (pressed_ && now_ms - first_ms_ <= kDoublePressMs) {
            disarm();
            return Gesture::Confirm;
        }
        pressed_ = true;
        first_ms_ = now_ms;
        return Gesture::None;
    }

    // INFO: cf 02aug26 A lone press is a refusal, not a nudge: the gesture a
    // pilot makes to change pages, made at a prompt, cancels the operation
    // rather than leaving it standing. Fail closed, and it is what makes
    // "press twice to allow, once to refuse" true on the panel.
    Gesture tick(uint32_t now_ms) {
        if (!armed_ || !pressed_) return Gesture::None;
        if (now_ms - first_ms_ < kDoublePressMs) return Gesture::None;
        disarm();
        return Gesture::Cancel;
    }

   private:
    uint32_t first_ms_{0};
    bool armed_{false};
    bool pressed_{false};
};

}  // namespace skyblip::ui

#endif
