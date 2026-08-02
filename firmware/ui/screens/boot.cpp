#include "ui/screens/boot.h"

#include "core/util/format.h"

namespace skyblip::ui {

namespace {

constexpr int kLeft = kBootLeftX;
constexpr int kRight = kBootRightX;
constexpr int kCellW = kBootCellW;
constexpr int kLineH = kBootRowH;

int length(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

void right_aligned(Framebuffer& fb, int x_end, int y, const char* text) {
    fb.draw_text(x_end - length(text) * kCellW, y, text, true, 1);
}

const char* verdict(PartState state) {
    switch (state) {
        case PartState::Pass: return "PASS";
        case PartState::Fail: return "FAIL";
        case PartState::Absent: break;
    }
    return "n/a";
}

// A leader of dots between the name and its verdict, so an eye running down the
// right-hand column does not lose which row it is on.
void leaders(Framebuffer& fb, int from_x, int to_x, int y) {
    for (int x = from_x; x < to_x; x += 2 * kCellW) fb.draw_text(x, y, ".", true, 1);
}

}  // namespace

void draw_boot(Framebuffer& fb, const BootSnapshot& s) {
    fb.clear(true);

    char buf[32];
    int n = fmt_string(buf, "SELF TEST");
    buf[n] = 0;
    fb.draw_text(kLeft, 3, buf, true, 2);
    fb.hline(kLeft, 21, Framebuffer::kW - 2 * kLeft, true);

    // Identity and why the device is running at all, on one line: the pair a
    // bench log needs before anything below it means something.
    n = fmt_string(buf, "ID ");
    n += fmt_hex(buf + n, s.device_addr, 6);
    buf[n] = 0;
    fb.draw_text(kLeft, kBootHeaderY, buf, true, 1);
    right_aligned(fb, kRight, kBootHeaderY, s.reset_reason);

    int y = kBootFirstRowY;
    const int rows = s.n_parts < kBootRows ? s.n_parts : kBootRows;
    for (int i = 0; i < rows; i++) {
        const BootPart& part = s.parts[i];
        const char* mark = verdict(part.state);
        fb.draw_text(kLeft, y, part.name, true, 1);
        leaders(fb, kLeft + (length(part.name) + 1) * kCellW, kRight - (length(mark) + 1) * kCellW,
                y);
        right_aligned(fb, kRight, y, mark);
        y += kLineH;
    }

    y += 3;
    fb.hline(kLeft, y, Framebuffer::kW - 2 * kLeft, true);
    y += 5;

    if (s.battery_valid) {
        // Centivolts: at boot the number that matters is whether the pack can
        // carry a flight, and two decimals is what a cell is judged on.
        n = fmt_string(buf, "BAT ");
        n += fmt_uint(buf + n, (s.battery_mv + 5u) / 10u, 1, 2);
        n += fmt_string(buf + n, " V");
        buf[n] = 0;
        fb.draw_text(kLeft, y, buf, true, 1);
    }

    // The verdict, inverted so it cannot be read as one more row.
    const char* status = s.flyable ? "READY" : "GROUNDED";
    const int w = (length(status) + 2) * kCellW;
    fb.rect(kRight - w, y - 3, w, 13, true, /*fill=*/true);
    fb.draw_text(kRight - w + kCellW, y, status, false, 1);
}

}  // namespace skyblip::ui
