// devices/models/ssd1681.h — a model of the SSD1681 e-paper panel at the
// SPI/GPIO seam, so the REAL drivers::Ssd1681 runs against it unchanged: it
// splits the stream on the D/C line, records every command, and captures what
// WriteRAM (0x24) put in panel RAM so polarity can be asserted.
//
// Higher fidelity but slower than models/display.h, which stops at hal::Display.
#ifndef SKYBLIP_DEVICES_MODELS_SSD1681_H
#define SKYBLIP_DEVICES_MODELS_SSD1681_H

#include <cstdint>
#include <vector>

#include "devices/io/io.h"

namespace skyblip::models {

class Ssd1681 : public io::Spi, public io::Gpio {
   public:
    int dc{0}, rst{1}, busy{2};

    void set(int pin, bool level) override {
        if (pin == dc) dc_high_ = level;
        if (pin == rst) {
            if (level && !rst_level_) reset_pulses++;
            rst_level_ = level;
        }
    }
    bool get(int pin) override { return pin == busy ? busy_stuck : false; }
    void mode_output(int) override {}
    void mode_input(int, bool) override {}

    void select(bool) override {}
    void transfer(const uint8_t* tx, uint8_t* rx, size_t len) override {
        for (size_t i = 0; i < len; i++) {
            uint8_t b = tx ? tx[i] : 0;
            if (rx) rx[i] = 0;
            if (!dc_high_) {
                cmds.push_back(b);
                capturing_ = (b == kWriteRam);
            } else {
                if (capturing_) ram.push_back(b);
            }
        }
    }

    bool saw_cmd(uint8_t c) const {
        for (uint8_t x : cmds)
            if (x == c) return true;
        return false;
    }

    std::vector<uint8_t> cmds;
    std::vector<uint8_t> ram;
    int reset_pulses{0};
    bool busy_stuck{false};

   private:
    static constexpr uint8_t kWriteRam = 0x24;

    bool dc_high_{false};
    bool rst_level_{true};
    bool capturing_{false};
};

}  // namespace skyblip::models

#endif
