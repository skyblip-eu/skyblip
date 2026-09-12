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
// INFO: fc 12sep26 0xFF ends a partial with the rails down, as Waveshare's own 0xCF does
constexpr uint8_t kSequenceFast = 0xFF;

// INFO: fc 09mar26 VBD follows LUT1 at 0x05 and greys over a run of partials; 0x80 holds it at VCOM
constexpr uint8_t kBorderFollowLut1 = 0x05;
constexpr uint8_t kBorderVcom = 0x80;

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

// INFO: fc 09mar26 GxEPD2 writes the new frame into both banks before a full refresh
const uint8_t* Ssd1681::previous_bank(const ui::Framebuffer& fb, bool full) const {
    return full ? fb.data() : shadow_;
}

void Ssd1681::present(const ui::Framebuffer& fb, hal::Refresh mode, uint32_t now_ms) {
    if (refreshing_) abort_refresh();
    if (asleep_) {
        init_panel();
        asleep_ = false;
    }

    const bool full = mode == hal::Refresh::Full || !glass_known_;

    set_window(0, 0, kW - 1, kH - 1);
    cmd(kBorderWaveform);
    data(full ? kBorderFollowLut1 : kBorderVcom);
    write_bank(kWriteRamPrevious, previous_bank(fb, full));
    write_bank(kWriteRam, fb.data());
    std::memcpy(shadow_, fb.data(), ui::Framebuffer::kBytes);

    cmd(kDisplayUpdateCtrl2);
    data(full ? kSequenceFull : kSequenceFast);
    cmd(kMasterActivation);

    glass_known_ = true;
    refreshing_ = true;
    fast_refresh_ = !full;
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
    if (refreshing_) abort_refresh();
    if (!wait_busy()) glass_known_ = false;
    if (!asleep_) enter_sleep();
}

// INFO: fc 13sep26 RES# is the only abort the glass has, and only a full waveform undoes the ink
void Ssd1681::abort_refresh() {
    refreshing_ = false;
    if (wait_busy()) return;
    init_panel();
    asleep_ = false;
    glass_known_ = false;
}

void Ssd1681::set_backlight(bool on) {
    if (backlight_ < 0) return;
    gpio_.mode_output(backlight_);
    gpio_.set(backlight_, on);
}

void Ssd1681::hold_reset() {
    for (uint32_t i = 0; i < epd::kResetHoldSpins; i++) (void)gpio_.get(busy_);
}

void Ssd1681::init_panel() {
    gpio_.set(rst_, true);
    gpio_.set(rst_, false);
    hold_reset();
    gpio_.set(rst_, true);
    hold_reset();
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
    data(kBorderFollowLut1);

    // INFO: fc 01aug25 internal sensor selects the temperature-compensated OTP LUT
    cmd(kTempSensorCtrl);
    data(0x80);

    set_cursor(0, 0);
    wait_busy();
}

void Ssd1681::finish_refresh() { refreshing_ = false; }

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
    uint8_t gate_line[ui::Framebuffer::kStride];
    gpio_.set(dc_, true);
    spi_.select(true);
    for (int gate = 0; gate < kH; gate++) {
        for (int column = 0; column < ui::Framebuffer::kStride; column++)
            gate_line[column] = static_cast<uint8_t>(~ram_byte(fb_bytes, gate, column));
        spi_.transfer(gate_line, nullptr, sizeof(gate_line));
    }
    spi_.select(false);
}

uint8_t Ssd1681::ram_byte(const uint8_t* fb_bytes, int gate, int column) const {
    if (rotation_ == GlassRotation::Deg0) return fb_bytes[gate * ui::Framebuffer::kStride + column];
    const int x = kW - 1 - gate;
    uint8_t bits = 0;
    for (int source = 0; source < 8; source++) {
        const int y = column * 8 + source;
        if (fb_bytes[y * ui::Framebuffer::kStride + (x >> 3)] & (0x80 >> (x & 7)))
            bits |= static_cast<uint8_t>(0x80 >> source);
    }
    return bits;
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

bool Ssd1681::wait_busy(uint32_t max_spins) {
    for (uint32_t i = 0; i < max_spins; i++) {
        if (!gpio_.get(busy_)) return true;
    }
    return false;
}

}  // namespace skyblip::parts
