#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_ANNUNCIATOR_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_ANNUNCIATOR_H

#include "hal/annunciator.h"

namespace skyblip::platform::host {

class Annunciator : public hal::Annunciator {
   public:
    void alarm(uint8_t level, uint8_t volume) override {
        level_ = level;
        volume_ = volume;
        tone_commands_++;
    }
    void vibrate(uint16_t ms) override {
        vibro_ms_ = ms;
        vibro_pulses_++;
    }
    void silence() override {
        level_ = 0;
        silences_++;
    }

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

   private:
    uint8_t level_{0}, volume_{0};
    uint16_t vibro_ms_{0};
    uint32_t tone_commands_{0}, silences_{0}, vibro_pulses_{0};
};

}  // namespace skyblip::platform::host

#endif
