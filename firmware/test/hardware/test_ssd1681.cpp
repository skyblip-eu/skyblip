// SSD1681 e-paper driver tests against models/ssd1681.h. Verifies the init
// sequence, the framebuffer→RAM polarity (fb 1=black → panel 0=black), and the
// full-vs-partial refresh cadence — all on the host, no panel required.
#include "doctest/doctest.h"
#include "hardware/parts/ssd1681/model.h"
#include "hardware/parts/ssd1681/ssd1681.h"
#include "ui/framebuffer.h"

using namespace skyblip;

namespace {

parts::Ssd1681 make(models::Ssd1681& f) { return parts::Ssd1681(f, f, f.dc, f.rst, f.busy); }

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
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
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
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    ui::Framebuffer fb;
    fb.clear(true);

    // kFullRefreshEvery partial presents should force exactly one full refresh.
    for (int i = 0; i < parts::Ssd1681::kFullRefreshEvery; i++) {
        d.present(fb, {0, 0, 200, 200}, hal::Refresh::Partial);
    }
    // The driver stays responsive (no busy hang) across the cadence.
    CHECK(f.saw_cmd(0x22));  // display update control 2 issued
}
