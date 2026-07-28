#ifndef SKYBLIP_HARDWARE_MODEL_SSD1681_H
#define SKYBLIP_HARDWARE_MODEL_SSD1681_H

#include <cstdint>
#include <cstdio>
#include <vector>

#include "hardware/io/io.h"
#include "ui/framebuffer.h"

namespace skyblip::models {

class Ssd1681 : public io::Spi, public io::Gpio {
   public:
    int dc{0}, rst{1}, busy{2}, backlight_pin{3};

    void set(int pin, bool level) override {
        if (pin == dc) dc_high_ = level;
        if (pin == backlight_pin) backlight = level;
        if (pin == rst) {
            if (level && !rst_level_) {
                reset_pulses++;
                powered = true;
            }
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
                capturing_ = b == kWriteRam;
                if (b == kWriteRam) ram.clear();
                if (b == kDeepSleep) powered = false;
                if (b == kMasterActivation) {
                    present_count++;
                    rasterise();
                }
                pending_ = b;
            } else {
                if (capturing_) ram.push_back(b);
                if (pending_ == kUpdateCtrl2) last_full = b == 0xF7;
            }
        }
    }

    bool saw_cmd(uint8_t c) const {
        for (uint8_t x : cmds)
            if (x == c) return true;
        return false;
    }

    // What the panel would be showing: RAM read back through the driver's own
    // inversion, so a polarity bug in the driver shows up as an inverted screen
    // in the simulator instead of passing unnoticed.
    const ui::Framebuffer& framebuffer() const { return panel_; }

    bool save_pgm(const char* path) const {
        FILE* f = std::fopen(path, "wb");
        if (!f) return false;
        std::fprintf(f, "P5\n%d %d\n255\n", ui::Framebuffer::kW, ui::Framebuffer::kH);
        for (int y = 0; y < ui::Framebuffer::kH; y++)
            for (int x = 0; x < ui::Framebuffer::kW; x++) {
                uint8_t v = panel_.get_pixel(x, y) ? 0 : 255;
                std::fwrite(&v, 1, 1, f);
            }
        std::fclose(f);
        return true;
    }

    std::vector<uint8_t> cmds;
    std::vector<uint8_t> ram;
    int reset_pulses{0};
    int present_count{0};
    bool busy_stuck{false};
    bool powered{true};
    bool backlight{false};
    bool last_full{true};

   private:
    static constexpr uint8_t kWriteRam = 0x24;
    static constexpr uint8_t kMasterActivation = 0x20;
    static constexpr uint8_t kUpdateCtrl2 = 0x22;
    static constexpr uint8_t kDeepSleep = 0x10;

    void rasterise() {
        if (ram.size() < ui::Framebuffer::kBytes) return;
        uint8_t* out = panel_.data();
        for (size_t i = 0; i < ui::Framebuffer::kBytes; i++)
            out[i] = static_cast<uint8_t>(~ram[i]);
    }

    ui::Framebuffer panel_{};
    bool dc_high_{false};
    bool rst_level_{true};
    bool capturing_{false};
    uint8_t pending_{0};
};

}  // namespace skyblip::models

#endif
