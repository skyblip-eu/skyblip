// ONCE and shared (like sx1262). Talks only to io::Spi / io::Gpio, so it runs
// unchanged on Zephyr, on the host (against models/ssd1681.h), and anywhere else.
//
// Command set per the Solomon SSD1681 datasheet + the GDEH0154D67 panel init
// sequence for the T-Echo panel. A present() writes both RAM banks — previous
// image from shadow_, new image from the framebuffer — kicks the refresh and
// returns; ready() polls BUSY and puts the panel to deep sleep once it settles.
#include "hardware/parts/ssd1681/ssd1681.h"

#include <cstring>

namespace skyblip::parts {

namespace {
constexpr uint8_t kDriverOutputCtrl = 0x01;
constexpr uint8_t kDataEntryMode = 0x11;
constexpr uint8_t kSwReset = 0x12;
constexpr uint8_t kTempSensorCtrl = 0x18;
constexpr uint8_t kMasterActivation = 0x20;
constexpr uint8_t kDisplayUpdateCtrl2 = 0x22;
constexpr uint8_t kWriteRam = 0x24;
constexpr uint8_t kWriteRamPrevious = 0x26;
constexpr uint8_t kBorderWaveform = 0x3C;
constexpr uint8_t kSetRamXAddr = 0x44;
constexpr uint8_t kSetRamYAddr = 0x45;
constexpr uint8_t kSetRamXCounter = 0x4E;
constexpr uint8_t kSetRamYCounter = 0x4F;
constexpr uint8_t kDeepSleep = 0x10;

// Display update control 2 sequences: full flashing waveform vs the fast
// differential one (display mode 2), both LUTs from the panel's OTP, both
// powering the analog rails down when done.
constexpr uint8_t kSequenceFull = 0xF7;
constexpr uint8_t kSequenceFast = 0xFF;

constexpr int kW = ui::Framebuffer::kW;  // 200
constexpr int kH = ui::Framebuffer::kH;  // 200
}  // namespace

void Ssd1681::begin() {
    gpio_.mode_output(dc_);
    gpio_.mode_output(rst_);
    gpio_.mode_input(busy_, false);
    init_panel();
    glass_known_ = false;
    refreshing_ = false;
    asleep_ = false;
}

void Ssd1681::present(const ui::Framebuffer& fb, hal::Refresh mode, uint32_t now_ms) {
    if (refreshing_) {  // only the shutdown path collides; let the glass settle
        wait_busy();
        finish_refresh();
    }
    if (asleep_) {
        init_panel();
        asleep_ = false;
    }

    const bool full = mode == hal::Refresh::Full || !glass_known_;

    set_window(0, 0, kW - 1, kH - 1);
    write_bank(kWriteRamPrevious, shadow_);
    write_bank(kWriteRam, fb.data());
    std::memcpy(shadow_, fb.data(), ui::Framebuffer::kBytes);

    cmd(kDisplayUpdateCtrl2);
    data(full ? kSequenceFull : kSequenceFast);
    cmd(kMasterActivation);

    glass_known_ = true;
    refreshing_ = true;
    ready_at_ms_ = now_ms + (full ? kReadyAfterFullMs : kReadyAfterFastMs);
    timeout_at_ms_ = now_ms + kBusyTimeoutMs;
}

bool Ssd1681::ready(uint32_t now_ms) {
    if (!refreshing_) return true;
    if (static_cast<int32_t>(now_ms - ready_at_ms_) < 0) return false;
    if (gpio_.get(busy_)) {
        if (static_cast<int32_t>(now_ms - timeout_at_ms_) < 0) return false;
        // BUSY stuck past any plausible refresh: re-initialise the panel. The
        // glass is in an unknown state, so the next present is forced full.
        init_panel();
        glass_known_ = false;
        refreshing_ = false;
        asleep_ = false;
        return true;
    }
    finish_refresh();
    return true;
}

void Ssd1681::power_off() {
    if (refreshing_) {
        wait_busy();
        finish_refresh();
        return;
    }
    if (!asleep_) enter_sleep();
}

void Ssd1681::set_backlight(bool on) {
    if (backlight_ < 0) return;
    gpio_.mode_output(backlight_);
    gpio_.set(backlight_, on);
}

void Ssd1681::init_panel() {
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
    data(0x80);  // internal sensor: the OTP LUT is temperature-compensated

    set_cursor(0, 0);
    wait_busy();
}

void Ssd1681::finish_refresh() {
    refreshing_ = false;
    // INFO: fc 01aug25 vendor guidance (Waveshare/Good Display): a panel left
    // powered between refreshes degrades irreversibly; deep sleep after every
    // update, image retained. present() wakes it with a reset pulse.
    enter_sleep();
}

void Ssd1681::enter_sleep() {
    cmd(kDeepSleep);
    data(0x01);  // mode 1: keep RAM powered enough to retain the image
    asleep_ = true;
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

// SSD1681 RAM is 1=white, 0=black; the framebuffer stores 1=black.
void Ssd1681::write_bank(uint8_t command, const uint8_t* fb_bytes) {
    set_cursor(0, 0);
    cmd(command);
    for (size_t i = 0; i < ui::Framebuffer::kBytes; i++) {
        data(static_cast<uint8_t>(~fb_bytes[i]));
    }
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
    // BUSY is high while the panel works. Bounded spin: used for the short init
    // waits and the shutdown path only — a running refresh is finished through
    // ready(), never waited on here.
    for (uint32_t i = 0; i < max_spins; i++) {
        if (!gpio_.get(busy_)) return;
    }
}

}  // namespace skyblip::parts
