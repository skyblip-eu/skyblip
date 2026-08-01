// SSD1681 e-paper driver tests against models/ssd1681.h. Verifies the init
// sequence, the framebuffer→RAM polarity (fb 1=black → panel 0=black), the
// two-bank differential contract (previous image in 0x26, new in 0x24), the
// non-blocking present/ready cycle, deep sleep between refreshes and the
// hung-BUSY recovery — all on the host, no panel required.
#include "doctest/doctest.h"
#include "hardware/parts/ssd1681/model.h"
#include "hardware/parts/ssd1681/ssd1681.h"
#include "ui/framebuffer.h"

using namespace skyblip;

namespace {

parts::Ssd1681 make(models::Ssd1681& f) { return parts::Ssd1681(f, f, f.dc, f.rst, f.busy); }

// Drives the present → ready cycle to completion, as the screen service would
// across ticks.
void settle(parts::Ssd1681& d, uint32_t issued_ms) {
    CHECK_FALSE(d.ready(issued_ms));
    CHECK(d.ready(issued_ms + parts::Ssd1681::kReadyAfterFullMs));
}

}  // namespace

TEST_CASE("epd: begin() runs the SSD1681 init sequence and resets the panel") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    CHECK(f.reset_pulses >= 1);
    CHECK(f.saw_cmd(0x12));  // SW reset
    CHECK(f.saw_cmd(0x01));  // driver output control
    CHECK(f.saw_cmd(0x11));  // data entry mode
    CHECK(f.saw_cmd(0x3C));  // border waveform
    CHECK(f.saw_cmd(0x18));  // temperature sensor
}

TEST_CASE("epd: present() writes a full framebuffer with correct black/white polarity") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();

    ui::Framebuffer fb;
    fb.clear(/*white=*/true);
    d.present(fb, hal::Refresh::Full, 0);

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
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();

    ui::Framebuffer fb;
    fb.clear(true);
    fb.set_pixel(0, 0, /*black=*/true);
    d.present(fb, hal::Refresh::Full, 0);

    // First RAM byte now has at least one cleared bit (was 0xFF all-white).
    CHECK(f.ram[0] != 0xFF);
}

TEST_CASE("epd: the first present after begin() is a full refresh, whatever was asked") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    ui::Framebuffer fb;
    fb.clear(true);

    // The glass content is unknown before the first full lands, so a fast
    // (differential) refresh would diff against garbage.
    d.present(fb, hal::Refresh::Fast, 0);
    CHECK(f.last_full);

    settle(d, 0);
    d.present(fb, hal::Refresh::Fast, 5000);
    CHECK_FALSE(f.last_full);
}

TEST_CASE("epd: present() rewrites the previous-image bank so the panel diffs the truth") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();

    ui::Framebuffer first;
    first.clear(true);
    first.set_pixel(10, 10, true);
    d.present(first, hal::Refresh::Full, 0);
    settle(d, 0);

    ui::Framebuffer second;
    second.clear(true);
    second.set_pixel(20, 20, true);
    d.present(second, hal::Refresh::Fast, 5000);

    // Bank 0x26 must hold what the glass shows — the first frame — and bank
    // 0x24 the new one, both in panel polarity. A stale or empty 0x26 is the
    // classic partial-update ghosting bug.
    REQUIRE(f.ram_previous.size() == ui::Framebuffer::kBytes);
    REQUIRE(f.ram.size() == ui::Framebuffer::kBytes);
    ui::Framebuffer glass;
    for (size_t i = 0; i < ui::Framebuffer::kBytes; i++)
        glass.data()[i] = static_cast<uint8_t>(~f.ram_previous[i]);
    CHECK(glass.get_pixel(10, 10));
    CHECK_FALSE(glass.get_pixel(20, 20));
}

TEST_CASE("epd: present() is non-blocking and ready() settles the panel into deep sleep") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    ui::Framebuffer fb;
    fb.clear(true);

    CHECK(d.ready(0));
    d.present(fb, hal::Refresh::Full, 1000);

    // Not ready before the panel can plausibly have finished, even though the
    // model's BUSY pin is already low.
    CHECK_FALSE(d.ready(1000));
    CHECK_FALSE(d.ready(1000 + parts::Ssd1681::kReadyAfterFullMs - 1));

    const int sleeps_before = f.deep_sleeps;
    CHECK(d.ready(1000 + parts::Ssd1681::kReadyAfterFullMs));
    // Vendor rule: never leave the panel powered between refreshes.
    CHECK(f.deep_sleeps == sleeps_before + 1);
    CHECK_FALSE(f.powered);
}

TEST_CASE("epd: a present after deep sleep wakes the panel with a reset pulse") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    ui::Framebuffer fb;
    fb.clear(true);

    d.present(fb, hal::Refresh::Full, 0);
    settle(d, 0);
    CHECK_FALSE(f.powered);

    const int resets_before = f.reset_pulses;
    fb.set_pixel(50, 50, true);
    d.present(fb, hal::Refresh::Fast, 5000);
    CHECK(f.reset_pulses == resets_before + 1);
    CHECK(f.powered);
    CHECK(f.present_count == 2);
}

TEST_CASE("epd: a hung BUSY line times out, re-initialises, and forces the next full") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    ui::Framebuffer fb;
    fb.clear(true);

    d.present(fb, hal::Refresh::Full, 0);
    settle(d, 0);

    d.present(fb, hal::Refresh::Fast, 5000);
    f.busy_stuck = true;
    CHECK_FALSE(d.ready(5000 + parts::Ssd1681::kReadyAfterFastMs));
    CHECK_FALSE(d.ready(5000 + parts::Ssd1681::kBusyTimeoutMs - 1));

    const int resets_before = f.reset_pulses;
    CHECK(d.ready(5000 + parts::Ssd1681::kBusyTimeoutMs));
    CHECK(f.reset_pulses > resets_before);

    // The glass is unknown after the recovery: the next present must be full.
    f.busy_stuck = false;
    d.present(fb, hal::Refresh::Fast, 20000);
    CHECK(f.last_full);
}

TEST_CASE("epd: power_off() parks a sleeping panel without touching it twice") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    ui::Framebuffer fb;
    fb.clear(true);

    d.present(fb, hal::Refresh::Full, 0);
    settle(d, 0);
    const int sleeps_before = f.deep_sleeps;
    d.power_off();
    CHECK(f.deep_sleeps == sleeps_before);  // already asleep: nothing to do
    CHECK_FALSE(f.powered);
}
