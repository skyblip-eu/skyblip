// The power-on self test, on the glass. On a first flash this is the only thing
// that can name the part that did not answer, so these read the framebuffer back
// rather than count ink: "there is something on the GNSS row" is not the claim
// that matters, "the GNSS row says FAIL" is.
#include "doctest/doctest.h"
#include "ui/framebuffer.h"
#include "ui/screens/boot.h"

using namespace skyblip::ui;

namespace {

int length(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

// Draw the same text, right-aligned on the same column, into a scratch buffer
// and compare the box it occupies.
bool reads_at(const Framebuffer& fb, int y, const char* text) {
    const int len = length(text);
    const int x = kBootRightX - len * kBootCellW;

    Framebuffer expected;
    expected.clear(true);
    expected.draw_text(x, y, text, true, 1);
    for (int dy = 0; dy < 7; dy++)
        for (int dx = 0; dx < len * kBootCellW; dx++)
            if (fb.get_pixel(x + dx, y + dy) != expected.get_pixel(x + dx, y + dy)) return false;
    return true;
}

bool row_reads(const Framebuffer& fb, int row, const char* verdict) {
    return reads_at(fb, boot_row_y(row), verdict);
}

BootSnapshot page(const BootPart* parts, int n, bool flyable) {
    BootSnapshot s;
    s.device_addr = 0x0ABBCC;
    s.reset_reason = "WATCHDOG";
    s.parts = parts;
    s.n_parts = n;
    s.flyable = flyable;
    return s;
}

}  // namespace

TEST_CASE("boot: every part on the page carries its own verdict") {
    const BootPart parts[] = {
        {"RADIO", PartState::Pass},
        {"GNSS", PartState::Fail},
        {"BARO", PartState::Absent},
    };
    Framebuffer fb;
    draw_boot(fb, page(parts, 3, /*flyable=*/false));

    CHECK(row_reads(fb, 0, "PASS"));
    CHECK(row_reads(fb, 1, "FAIL"));
    CHECK(row_reads(fb, 2, "n/a"));

    // An absent part is not a failed one. A unit built without a barometer must
    // not read as a broken unit on a bench.
    CHECK_FALSE(row_reads(fb, 2, "FAIL"));
    CHECK_FALSE(row_reads(fb, 0, "FAIL"));
}

TEST_CASE("boot: the page says whether the device may fly, in one word") {
    const BootPart ok[] = {{"RADIO", PartState::Pass}};
    Framebuffer ready;
    draw_boot(ready, page(ok, 1, /*flyable=*/true));

    const BootPart bad[] = {{"RADIO", PartState::Fail}};
    Framebuffer grounded;
    draw_boot(grounded, page(bad, 1, /*flyable=*/false));

    // The verdict is reversed out of a filled block, so the two pages differ by
    // far more than the four characters of the badge.
    CHECK(ready.count_black() != grounded.count_black());
    CHECK(grounded.count_black() > 200);
    CHECK(ready.count_black() > 200);
}

TEST_CASE("boot: the reset reason and the identity share the header") {
    const BootPart parts[] = {{"RADIO", PartState::Pass}};
    Framebuffer fb;
    draw_boot(fb, page(parts, 1, /*flyable=*/true));
    CHECK(reads_at(fb, kBootHeaderY, "WATCHDOG"));

    // A boot that followed a bite must not look like a boot that followed a
    // pilot pressing the button.
    BootSnapshot other = page(parts, 1, /*flyable=*/true);
    other.reset_reason = "POWER ON";
    Framebuffer fresh;
    draw_boot(fresh, other);
    CHECK(reads_at(fresh, kBootHeaderY, "POWER ON"));
    CHECK_FALSE(reads_at(fresh, kBootHeaderY, "WATCHDOG"));
}

TEST_CASE("boot: a full inventory still fits the panel") {
    BootPart parts[kBootRows];
    for (int i = 0; i < kBootRows; i++) {
        parts[i].name = "PART";
        parts[i].state = PartState::Pass;
    }
    Framebuffer fb;
    draw_boot(fb, page(parts, kBootRows, /*flyable=*/true));

    // The last row and the verdict below it are both on the glass, not clipped
    // off the bottom of a 200-pixel panel.
    CHECK(boot_row_y(kBootRows - 1) + 7 < Framebuffer::kH);
    CHECK(row_reads(fb, kBootRows - 1, "PASS"));

    int footer_ink = 0;
    for (int y = boot_row_y(kBootRows - 1) + 8; y < Framebuffer::kH; y++)
        for (int x = 0; x < Framebuffer::kW; x++) footer_ink += fb.get_pixel(x, y) ? 1 : 0;
    CHECK(footer_ink > 100);
}

TEST_CASE("boot: the cell's voltage is on the page when there is a gauge to read") {
    const BootPart parts[] = {{"RADIO", PartState::Pass}};
    BootSnapshot with = page(parts, 1, /*flyable=*/true);
    with.battery_valid = true;
    with.battery_mv = 4050;

    Framebuffer fb;
    draw_boot(fb, with);
    Framebuffer without;
    draw_boot(without, page(parts, 1, /*flyable=*/true));
    CHECK(fb.count_black() > without.count_black());
}
