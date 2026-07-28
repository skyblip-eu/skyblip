#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_ANNUNCIATOR_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_ANNUNCIATOR_H

#include "hal/annunciator.h"

namespace skyblip::platform::host {

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

}  // namespace skyblip::platform::host

#endif
