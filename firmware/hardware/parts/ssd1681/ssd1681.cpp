// ONCE and shared (like sx1262). Talks only to io::Spi / io::Gpio, so it runs
// unchanged on Zephyr, on the host (against models/ssd1681.h), and anywhere else.
//
// Command set per the Solomon SSD1681 datasheet + the GDEH0154D67 panel init
// sequence for the T-Echo panel. Full and partial (fast) refresh.
#include "hardware/parts/ssd1681/ssd1681.h"

#include "ui/framebuffer.h"

namespace skyblip::parts {

namespace {
constexpr uint8_t kDriverOutputCtrl = 0x01;
constexpr uint8_t kDataEntryMode = 0x11;
constexpr uint8_t kSwReset = 0x12;
constexpr uint8_t kTempSensorCtrl = 0x18;
constexpr uint8_t kMasterActivation = 0x20;
constexpr uint8_t kDisplayUpdateCtrl2 = 0x22;
constexpr uint8_t kWriteRam = 0x24;
constexpr uint8_t kBorderWaveform = 0x3C;
constexpr uint8_t kSetRamXAddr = 0x44;
constexpr uint8_t kSetRamYAddr = 0x45;
constexpr uint8_t kSetRamXCounter = 0x4E;
constexpr uint8_t kSetRamYCounter = 0x4F;
constexpr uint8_t kDeepSleep = 0x10;

constexpr int kW = ui::Framebuffer::kW;  // 200
constexpr int kH = ui::Framebuffer::kH;  // 200
}  // namespace

void Ssd1681::begin() {
    gpio_.mode_output(dc_);
    gpio_.mode_output(rst_);
    gpio_.mode_input(busy_, false);

    // Hardware reset pulse.
    gpio_.set(rst_, true);
    gpio_.set(rst_, false);
    gpio_.set(rst_, true);
    wait_busy();

    cmd(kSwReset);
    wait_busy();

    // 200 lines, gate scan settings.
    cmd(kDriverOutputCtrl);
    data(0xC7);  // (200-1) low byte
    data(0x00);  // high byte
    data(0x00);  // GD=0, SM=0, TB=0

    cmd(kDataEntryMode);
    data(0x03);  // X inc, Y inc

    set_window(0, 0, kW - 1, kH - 1);

    cmd(kBorderWaveform);
    data(0x05);

    cmd(kTempSensorCtrl);
    data(0x80);  // internal temperature sensor

    set_cursor(0, 0);
    wait_busy();
}

void Ssd1681::present(const ui::Framebuffer& fb, hal::Rect /*region*/, hal::Refresh mode) {
    // SSD1681 "black" RAM is 1=white, 0=black; our framebuffer stores 1=black.
    // The panel is refreshed as a full frame; partial region is honored by the
    // update-mode selection (fast LUT), not by a windowed RAM write here.
    set_window(0, 0, kW - 1, kH - 1);
    set_cursor(0, 0);

    cmd(kWriteRam);
    const uint8_t* buf = fb.data();
    for (size_t i = 0; i < ui::Framebuffer::kBytes; i++) {
        data(static_cast<uint8_t>(~buf[i]));  // invert: fb 1=black → panel 0=black
    }

    bool full = mode == hal::Refresh::Full || (++partials_since_full_ >= kFullRefreshEvery);
    if (full) partials_since_full_ = 0;

    cmd(kDisplayUpdateCtrl2);
    data(full ? 0xF7 : 0xFF);  // full vs partial (fast) update sequence
    cmd(kMasterActivation);
    wait_busy();
}

void Ssd1681::set_backlight(bool on) {
    if (backlight_ < 0) return;
    gpio_.mode_output(backlight_);
    gpio_.set(backlight_, on);
}

void Ssd1681::power_off() {
    cmd(kDeepSleep);
    data(0x01);
}

// ---- low-level helpers ------------------------------------------------------

void Ssd1681::cmd(uint8_t c) {
    gpio_.set(dc_, false);  // command
    spi_.select(true);
    spi_.transfer(&c, nullptr, 1);
    spi_.select(false);
}

void Ssd1681::data(uint8_t d) {
    gpio_.set(dc_, true);  // data
    spi_.select(true);
    spi_.transfer(&d, nullptr, 1);
    spi_.select(false);
}

void Ssd1681::set_window(int x0, int y0, int x1, int y1) {
    cmd(kSetRamXAddr);
    data(static_cast<uint8_t>(x0 / 8));
    data(static_cast<uint8_t>(x1 / 8));
    cmd(kSetRamYAddr);
    data(static_cast<uint8_t>(y0));
    data(static_cast<uint8_t>(y0 >> 8));
    data(static_cast<uint8_t>(y1));
    data(static_cast<uint8_t>(y1 >> 8));
}

void Ssd1681::set_cursor(int x, int y) {
    cmd(kSetRamXCounter);
    data(static_cast<uint8_t>(x / 8));
    cmd(kSetRamYCounter);
    data(static_cast<uint8_t>(y));
    data(static_cast<uint8_t>(y >> 8));
}

void Ssd1681::wait_busy(uint32_t max_spins) {
    // BUSY is high while the panel is working. Bounded spin (the panel model drives it
    // low immediately; real panel needs a few hundred ms — the shell may yield).
    for (uint32_t i = 0; i < max_spins; i++) {
        if (!gpio_.get(busy_)) return;
    }
}

}  // namespace skyblip::parts
