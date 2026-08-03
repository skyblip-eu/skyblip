// Power-on self test, on the glass. The one page worth having on a first flash:
// it names the part that did not answer, so a device that refuses to fly says
// why instead of going dark.
#ifndef SKYBLIP_UI_SCREENS_BOOT_H
#define SKYBLIP_UI_SCREENS_BOOT_H

#include <cstdint>

#include "ui/framebuffer.h"

namespace skyblip::ui {

// What a part answered at bring-up. Absent is not a failure: a unit built
// without a barometer is a unit without a barometer, and the page has to be able
// to say which of the two it is looking at.
enum class PartState : uint8_t { Pass, Absent, Fail };

struct BootPart {
    const char* name{""};
    PartState state{PartState::Absent};
};

struct BootSnapshot {
    uint32_t device_addr{0};
    const char* reset_reason{"UNKNOWN"};
    const BootPart* parts{nullptr};
    int n_parts{0};
    // False when a required part is missing. The device stays up and keeps this
    // page: a panel naming the failure is worth more than a reboot loop.
    bool flyable{true};
    bool battery_valid{false};
    uint16_t battery_mv{0};
};

// The most rows the page can carry before the last one falls off the panel.
constexpr int kBootRows = 11;

// The grid the page is set on, exported so a test can read a row back off the
// glass instead of counting ink: "there is ink on the GNSS row" is not the same
// claim as "the GNSS row says FAIL".
constexpr int kBootLeftX = 4;
constexpr int kBootRightX = Framebuffer::kW - kBootLeftX;
constexpr int kBootCellW = 6;  // the 5x7 font's advance at scale 1
constexpr int kBootRowH = 12;
constexpr int kBootFirstRowY = 40;
constexpr int kBootHeaderY = 26;

constexpr int boot_row_y(int row) { return kBootFirstRowY + row * kBootRowH; }

void draw_boot(Framebuffer& fb, const BootSnapshot& snapshot);

}  // namespace skyblip::ui

#endif
