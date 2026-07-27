// devices/models/annunciator.h — a model of the buzzer + vibration motor: it
// records the level, volume and buzz duration it was driven with, so a test can
// assert the alarm fired and a simulator frontend can show it.
#ifndef SKYBLIP_DEVICES_MODELS_ANNUNCIATOR_H
#define SKYBLIP_DEVICES_MODELS_ANNUNCIATOR_H

#include "hal/annunciator.h"

namespace skyblip::models {

class Annunciator : public hal::Annunciator {
   public:
    void alarm(uint8_t level, uint8_t volume) override {
        level_ = level;
        volume_ = volume;
    }
    void vibrate(uint16_t ms) override { vibro_ms_ = ms; }
    void silence() override { level_ = 0; }

    uint8_t level() const { return level_; }
    uint8_t volume() const { return volume_; }
    uint16_t vibro_ms() const { return vibro_ms_; }

   private:
    uint8_t level_{0}, volume_{0};
    uint16_t vibro_ms_{0};
};

}  // namespace skyblip::models

#endif
