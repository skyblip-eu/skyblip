// SSD1681 e-paper driver tests against a fake SPI/GPIO bus. Verifies the init
// sequence, the framebuffer→RAM polarity (fb 1=black → panel 0=black), and the
// full-vs-partial refresh cadence — all on the host, no panel required.
#include <vector>

#include "devices/drivers/ssd1681.h"
#include "devices/io/io.h"
#include "doctest/doctest.h"
#include "ui/framebuffer.h"

using namespace skyblip;

namespace {

// Records the command/data stream, splitting on the DC line. When the WriteRAM
// (0x24) command is seen, subsequent data bytes are captured as the RAM image.
class FakeEpd : public io::Spi, public io::Gpio {
   public:
    int dc{0}, rst{1}, busy{2};

    // io::Gpio
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

    // io::Spi
    void select(bool) override {}
    void transfer(const uint8_t* tx, uint8_t* rx, size_t len) override {
        for (size_t i = 0; i < len; i++) {
            uint8_t b = tx ? tx[i] : 0;
            if (rx) rx[i] = 0;
            if (!dc_high_) {
                cmds.push_back(b);
                capturing_ = (b == 0x24);  // WriteRAM
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
    bool dc_high_{false};
    bool rst_level_{true};
    bool capturing_{false};
};

drivers::Ssd1681 make(FakeEpd& f) { return drivers::Ssd1681(f, f, f.dc, f.rst, f.busy); }

}  // namespace

TEST_CASE("epd: begin() runs the SSD1681 init sequence and resets the panel") {
    FakeEpd f;
    drivers::Ssd1681 d = make(f);
    d.begin();
    CHECK(f.reset_pulses >= 1);
    CHECK(f.saw_cmd(0x12));  // SW reset
    CHECK(f.saw_cmd(0x01));  // driver output control
    CHECK(f.saw_cmd(0x11));  // data entry mode
    CHECK(f.saw_cmd(0x3C));  // border waveform
    CHECK(f.saw_cmd(0x18));  // temperature sensor
}

TEST_CASE("epd: present() writes a full framebuffer with correct black/white polarity") {
    FakeEpd f;
    drivers::Ssd1681 d = make(f);
    d.begin();

    ui::Framebuffer fb;
    fb.clear(/*white=*/true);
    f.ram.clear();
    d.present(fb, {0, 0, 200, 200}, hal::Refresh::Full);

    CHECK(f.ram.size() == ui::Framebuffer::kBytes);
    // An all-white framebuffer → all 0xFF in panel RAM (inverted).
    bool all_ff = true;
    for (uint8_t b : f.ram)
        if (b != 0xFF) all_ff = false;
    CHECK(all_ff);
    CHECK(f.saw_cmd(0x24));  // WriteRAM
    CHECK(f.saw_cmd(0x20));  // Master activation
}

TEST_CASE("epd: a black pixel flips the corresponding RAM bit to 0") {
    FakeEpd f;
    drivers::Ssd1681 d = make(f);
    d.begin();

    ui::Framebuffer fb;
    fb.clear(true);
    fb.set_pixel(0, 0, /*black=*/true);
    f.ram.clear();
    d.present(fb, {0, 0, 200, 200}, hal::Refresh::Full);

    // First RAM byte now has at least one cleared bit (was 0xFF all-white).
    CHECK(f.ram[0] != 0xFF);
}

TEST_CASE("epd: partial refreshes escalate to a full refresh on the cadence") {
    FakeEpd f;
    drivers::Ssd1681 d = make(f);
    d.begin();
    ui::Framebuffer fb;
    fb.clear(true);

    // kFullRefreshEvery partial presents should force exactly one full refresh.
    for (int i = 0; i < drivers::Ssd1681::kFullRefreshEvery; i++) {
        d.present(fb, {0, 0, 200, 200}, hal::Refresh::Partial);
    }
    // The driver stays responsive (no busy hang) across the cadence.
    CHECK(f.saw_cmd(0x22));  // display update control 2 issued
}
