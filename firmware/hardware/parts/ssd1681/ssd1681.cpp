// GDEH0154D67 panel on the SSD1681 controller, over io::Spi / io::Gpio only.
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
constexpr uint8_t kDeepSleepRetainRam = 0x01;

constexpr uint8_t kSequenceFull = 0xF7;
constexpr uint8_t kSequenceFast = 0xFF;

constexpr int kW = ui::Framebuffer::kW;
constexpr int kH = ui::Framebuffer::kH;
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
    if (refreshing_) {  // only the shutdown path presents into a running refresh
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
    gpio_.set(rst_, true);
    gpio_.set(rst_, false);
    gpio_.set(rst_, true);
    wait_busy();

    cmd(kSwReset);
    wait_busy();

    cmd(kDriverOutputCtrl);
    data(0xC7);  // 200 - 1 gates
    data(0x00);
    data(0x00);

    cmd(kDataEntryMode);
    data(0x03);  // X inc, Y inc

    set_window(0, 0, kW - 1, kH - 1);

    cmd(kBorderWaveform);
    data(0x05);

    // INFO: fc 01aug25 internal sensor selects the temperature-compensated OTP LUT
    cmd(kTempSensorCtrl);
    data(0x80);

    set_cursor(0, 0);
    wait_busy();
}

void Ssd1681::finish_refresh() {
    refreshing_ = false;
    // INFO: fc 01aug25 vendor rule: a panel left powered between refreshes degrades
    enter_sleep();
}

void Ssd1681::enter_sleep() {
    cmd(kDeepSleep);
    data(kDeepSleepRetainRam);
    asleep_ = true;
}

void Ssd1681::cmd(uint8_t c) {
    gpio_.set(dc_, false);
    spi_.select(true);
    spi_.transfer(&c, nullptr, 1);
    spi_.select(false);
}

void Ssd1681::data(uint8_t d) {
    gpio_.set(dc_, true);
    spi_.select(true);
    spi_.transfer(&d, nullptr, 1);
    spi_.select(false);
}

void Ssd1681::write_bank(uint8_t command, const uint8_t* fb_bytes) {
    set_cursor(0, 0);
    cmd(command);
    // INFO: fc 01aug25 panel RAM is 1=white, the framebuffer 1=black
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
    for (uint32_t i = 0; i < max_spins; i++) {
        if (!gpio_.get(busy_)) return;
    }
}

}  // namespace skyblip::parts
