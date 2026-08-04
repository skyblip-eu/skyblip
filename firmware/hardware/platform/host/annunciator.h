#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_ANNUNCIATOR_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_ANNUNCIATOR_H

#include <cstdint>

#include "hal/annunciator.h"
#include "hal/haptic.h"

namespace skyblip::platform::host {

// A vibration motor wired straight to a pin: on while it is driven. The virtual
// twin of what the silicon platform can do with a GPIO and a timer, and what the
// T-Echo Plus turns out NOT to be fitted with.
class PinMotor : public hal::Haptic {
   public:
    void start() override { driving = true; }
    void stop() override { driving = false; }

    bool driving{false};
};

class Annunciator : public hal::Annunciator {
   public:
    void alarm(uint8_t level, uint8_t volume) override {
        level_ = level;
        volume_ = volume;
        tone_commands_++;
    }

    // The pulse and its end, both here: whatever the haptic is, the length of the
    // pulse is the adapter's to own (hal/annunciator.h). The host has no timer,
    // so the end is the caller's next tick asking for the time - which is what
    // service() is, driven by the board's poll.
    void vibrate(uint16_t ms) override {
        vibro_ms_ = ms;
        vibro_pulses_++;
        haptic_->start();
        pulse_ends_ms_ = ms;
        pulsing_ = true;
    }

    void silence() override {
        level_ = 0;
        silences_++;
    }

    // Ends a pulse whose time is up. The silicon platform has a kernel timer for
    // this; the host has the loop, so the board calls it once per pass. The
    // deadline is a duration from the first pass after the pulse started, so a
    // test that never advances the clock leaves the motor running, exactly as the
    // hardware would.
    void service(uint32_t now_ms) {
        if (!pulsing_) return;
        if (!started_) {
            started_ = true;
            start_ms_ = now_ms;
        }
        if (now_ms - start_ms_ < pulse_ends_ms_) return;
        haptic_->stop();
        pulsing_ = false;
        started_ = false;
    }

    void attach_haptic(hal::Haptic& haptic) { haptic_ = &haptic; }

    uint8_t level() const { return level_; }
    uint8_t volume() const { return volume_; }
    uint16_t vibro_ms() const { return vibro_ms_; }

    // The real part opens a continuous PWM tone on alarm() and closes it on
    // silence(), so a pattern is countable: these are how a test tells one tone
    // with a cadence from the same tone re-armed on every pass of the loop, and
    // one buzz of the motor from a motor buzzed on every re-announcement.
    uint32_t tone_commands() const { return tone_commands_; }
    uint32_t silences() const { return silences_; }
    uint32_t vibro_pulses() const { return vibro_pulses_; }
    PinMotor& pin_motor() { return pin_motor_; }

   private:
    uint8_t level_{0}, volume_{0};
    uint16_t vibro_ms_{0};
    uint32_t tone_commands_{0}, silences_{0}, vibro_pulses_{0};
    PinMotor pin_motor_{};
    hal::Haptic* haptic_{&pin_motor_};
    uint32_t start_ms_{0};
    uint32_t pulse_ends_ms_{0};
    bool pulsing_{false};
    bool started_{false};
};

}  // namespace skyblip::platform::host

#endif
