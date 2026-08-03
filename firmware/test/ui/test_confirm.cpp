// The authorisation prompt, read back off the glass. With BLE pairing off, this
// page is the only thing standing between a stranger's phone and the secondary
// slot, so "there is ink on the panel" is not the claim: "the panel says POWER
// OFF, and says which press allows it" is.
#include "core/comms/config.h"
#include "doctest/doctest.h"
#include "ui/framebuffer.h"
#include "ui/screens/confirm.h"

using namespace skyblip;
using namespace skyblip::ui;

namespace {

int length(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

// Draw the same text at the same place into a scratch buffer and compare the
// box it occupies.
bool reads_at(const Framebuffer& fb, int x, int y, const char* text, int scale) {
    Framebuffer expected;
    expected.clear(true);
    expected.draw_text(x, y, text, true, scale);
    for (int dy = 0; dy < 7 * scale; dy++)
        for (int dx = 0; dx < length(text) * kConfirmCellW * scale; dx++)
            if (fb.get_pixel(x + dx, y + dy) != expected.get_pixel(x + dx, y + dy)) return false;
    return true;
}

ConfirmSnapshot prompt(comms::Pending pending) {
    ConfirmSnapshot s;
    s.title = comms::pending_title(pending);
    s.detail = comms::pending_detail(pending);
    s.timeout_s = comms::kConfirmWindowMs / 1000;
    return s;
}

}  // namespace

TEST_CASE("confirm page: the pilot is told which operation they are authorising") {
    Framebuffer dfu;
    draw_confirm(dfu, prompt(comms::Pending::Dfu));
    CHECK(
        reads_at(dfu, kConfirmLeftX, kConfirmTitleY, comms::pending_title(comms::Pending::Dfu), 2));

    // Confirming a firmware upload must not look like confirming a shutdown.
    Framebuffer off;
    draw_confirm(off, prompt(comms::Pending::PowerOff));
    CHECK(reads_at(off, kConfirmLeftX, kConfirmTitleY,
                   comms::pending_title(comms::Pending::PowerOff), 2));
    CHECK_FALSE(
        reads_at(off, kConfirmLeftX, kConfirmTitleY, comms::pending_title(comms::Pending::Dfu), 2));
}

TEST_CASE("confirm page: what it will do is on the page, in words, and not clipped") {
    for (comms::Pending pending : {comms::Pending::Set, comms::Pending::Dfu, comms::Pending::Apply,
                                   comms::Pending::Recovery, comms::Pending::PowerOff}) {
        Framebuffer fb;
        draw_confirm(fb, prompt(pending));

        // Every word of the detail reaches the glass: the sentence wraps rather
        // than running off the right-hand edge or off the bottom.
        const char* detail = comms::pending_detail(pending);
        int drawn = 0;
        for (int row = 0; row < kConfirmDetailRows; row++) {
            const int y = confirm_detail_y(row);
            for (int x = kConfirmLeftX; x < Framebuffer::kW; x++)
                for (int dy = 0; dy < 7; dy++) drawn += fb.get_pixel(x, y + dy) ? 1 : 0;
        }
        CHECK(drawn > 0);
        CHECK(length(detail) <= kConfirmDetailCols * kConfirmDetailRows);
        CHECK(confirm_detail_y(kConfirmDetailRows - 1) + 7 < kConfirmAllowY);
    }
}

TEST_CASE("confirm page: the allowing gesture and the refusing one are both spelled out") {
    Framebuffer fb;
    draw_confirm(fb, prompt(comms::Pending::Apply));

    // The allowing line is reversed out of a filled block, so it is white ink.
    CHECK_FALSE(reads_at(fb, kConfirmLeftX + kConfirmCellW, kConfirmAllowY, kConfirmAllowText, 1));
    Framebuffer inverted;
    inverted.clear(true);
    inverted.rect(kConfirmLeftX, kConfirmAllowY - 4,
                  (length(kConfirmAllowText) + 2) * kConfirmCellW, 15, true, /*fill=*/true);
    inverted.draw_text(kConfirmLeftX + kConfirmCellW, kConfirmAllowY, kConfirmAllowText, false, 1);
    bool same = true;
    for (int y = kConfirmAllowY - 4; y < kConfirmAllowY + 11; y++)
        for (int x = kConfirmLeftX; x < kConfirmLeftX + 130; x++)
            if (fb.get_pixel(x, y) != inverted.get_pixel(x, y)) same = false;
    CHECK(same);

    CHECK(reads_at(fb, kConfirmLeftX + kConfirmCellW, kConfirmRefuseY, kConfirmRefuseText, 1));

    // And the page says it will refuse on its own, so a prompt left standing is
    // not read as one waiting indefinitely.
    CHECK(reads_at(fb, kConfirmLeftX, kConfirmExpiryY, "REFUSED AFTER 30 S", 1));
}

TEST_CASE("confirm page: it cannot be mistaken for a page a pilot was already on") {
    Framebuffer fb;
    draw_confirm(fb, prompt(comms::Pending::Recovery));

    // A full-width reversed header: at arm's length the shape alone says the
    // device is asking something rather than showing something.
    int header_ink = 0;
    for (int y = 0; y < 26; y++)
        for (int x = 0; x < Framebuffer::kW; x++) header_ink += fb.get_pixel(x, y) ? 1 : 0;
    CHECK(header_ink > 26 * Framebuffer::kW / 2);
}
