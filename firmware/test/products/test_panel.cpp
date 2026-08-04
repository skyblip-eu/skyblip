// What reaches the glass, driven through the whole product rather than through a
// screen in isolation: the pages a button walks, the self test that names the
// part which did not answer, the unit a pilot reads an instrument in, and the
// image an e-paper wears once its rails are down. A page that is right in a
// widget test and never presented is a page nobody sees.
#include "doctest/doctest.h"
#include "test/support/product_rig.h"
#include "ui/screens/boot.h"
#include "ui/widgets/wordmark.h"

using namespace skyblip;

TEST_CASE("product: a button press switches page and the layout swap lands full") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.product.screen().page() == go::Page::Radar);

    uint32_t t = 100;
    rig.press(t);
    CHECK(rig.product.screen().page() == go::Page::SixPack);
    rig.run(t, t + 4000);  // panel settles, floor passes: the page-change full lands
    t += 4000;
    CHECK(rig.platform.chips().epd.last_full);
    CHECK(rig.product.screen().fasts_since_full() == 0);

    rig.press(t);
    CHECK(rig.product.screen().page() == go::Page::Status);
    rig.press(t);
    CHECK(rig.product.screen().page() == go::Page::Signal);

    // Every page is on the rotation by default, and the settings page closes
    // it. From there the button belongs to the rows rather than to the
    // rotation, and the way back to the traffic picture is walking them out -
    // which is test/products/test_settings_page.cpp's business.
    rig.press(t);
    CHECK(rig.product.screen().page() == go::Page::Settings);
}

TEST_CASE("product: page_mask disables pages so the button skips them") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.state().settings.page_mask = 0x05;  // radar + status only
    uint32_t t = 100;
    rig.press(t);
    CHECK(rig.product.screen().page() == go::Page::Status);

    // The mask has no bit for the settings page and never will: it is where the
    // mask itself is changed, so hiding it would be a decision nothing on the
    // device could undo.
    rig.press(t);
    CHECK(rig.product.screen().page() == go::Page::Settings);
}

TEST_CASE("product: powering the panel down leaves the wordmark on it") {
    // An e-paper holds its last image with the rails down, so what is written
    // immediately before power_off is what the device wears while it is off.
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 1000);

    ui::Framebuffer expected;
    expected.clear(true);
    ui::draw_wordmark(expected, ui::Framebuffer::kW / 2, ui::Framebuffer::kH / 2);

    rig.product.screen().set_power(false);
    CHECK_FALSE(rig.platform.chips().epd.powered);
    CHECK(rig.platform.chips().epd.last_full);
    CHECK(rig.platform.chips().epd.framebuffer().count_black() == expected.count_black());
    CHECK(expected.count_black() > 200);

    // ... and it stays there: a service that keeps rendering must not reach a
    // panel whose rails are down, or the mark would be wiped a second later.
    rig.run(1000, 4000);
    CHECK(rig.platform.chips().epd.framebuffer().count_black() == expected.count_black());

    // The mark is above the word: the dot and its arcs put ink in the top half
    // of the glyph block, which the letters alone would leave blank.
    int arc_ink = 0;
    for (int y = 66; y < 78; y++)
        for (int x = 100; x < 160; x++) arc_ink += expected.get_pixel(x, y) ? 1 : 0;
    CHECK(arc_ink > 20);
}

TEST_CASE("product: the backlight starts off") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 1000);
    CHECK_FALSE(rig.product.screen().backlight());
    CHECK_FALSE(rig.platform.chips().epd.backlight);
}

// D2: the panel is the self test. A device that returns from main and goes dark
// tells a pilot on a bench nothing at all; a device holding a page that names
// the part that did not answer tells them everything.

// D2: the panel is the self test. A device that returns from main and goes dark
// tells a pilot on a bench nothing at all; a device holding a page that names
// the part that did not answer tells them everything.

TEST_CASE("product: the self-test page reaches the panel before anything refuses") {
    constexpr hal::Capabilities kNoGnss = static_cast<hal::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(hal::Capability::Gnss));
    Rig rig{kNoGnss};
    CHECK(rig.setup() == Status::Down);
    CHECK_FALSE(rig.product.flyable());

    // Painted, full, and it is the self-test page rather than a blank glass.
    CHECK(rig.platform.chips().epd.present_count == 1);
    CHECK(rig.platform.chips().epd.last_full);
    CHECK(rig.platform.chips().epd.framebuffer().count_black() ==
          rig.product.boot_page().count_black());
    CHECK(rig.product.boot_page().count_black() > 200);

    // And it stays. The loop refuses to fly, so nothing overwrites the one page
    // that says why.
    rig.run(0, 20000);
    CHECK(rig.platform.chips().epd.present_count == 1);
    CHECK_FALSE(rig.state().started);
}

TEST_CASE("product: the self-test page names the part, not just the failure") {
    constexpr hal::Capabilities kNoGnss = static_cast<hal::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(hal::Capability::Gnss));
    Rig missing{kNoGnss};
    missing.setup();

    Rig whole;
    whole.setup();

    // Row 1 is GNSS (products/skyblip_go/product.h::kBootParts). The two pages
    // differ there and nowhere else in that row's band.
    const ui::Framebuffer& bad = missing.product.boot_page();
    const ui::Framebuffer& good = whole.product.boot_page();
    int row_difference = 0;
    for (int y = ui::boot_row_y(1); y < ui::boot_row_y(1) + 8; y++)
        for (int x = 0; x < ui::Framebuffer::kW; x++)
            row_difference += bad.get_pixel(x, y) != good.get_pixel(x, y) ? 1 : 0;
    CHECK(row_difference > 0);

    // The radio row is identical on both: only the part that failed changed.
    int radio_difference = 0;
    for (int y = ui::boot_row_y(0); y < ui::boot_row_y(0) + 8; y++)
        for (int x = 0; x < ui::Framebuffer::kW; x++)
            radio_difference += bad.get_pixel(x, y) != good.get_pixel(x, y) ? 1 : 0;
    CHECK(radio_difference == 0);
}

// B4. The one page where a value appears once, so the one page the unit setting
// can decide. What is on the glass changes; the status page's two columns do not.
TEST_CASE("product: the unit setting changes the instrument page a pilot reads") {
    auto sixpack_ink = [](settings::Units units) {
        Rig rig;
        REQUIRE(rig.setup() == Status::Ok);
        rig.state().settings.units = units;
        uint32_t t = 100;
        rig.press(t);  // radar -> six-pack
        rig.run(t, t + 3000);
        REQUIRE(rig.product.screen().page() == go::Page::SixPack);
        return rig.product.screen().framebuffer().count_black();
    };

    CHECK(sixpack_ink(settings::Units::Metric) != sixpack_ink(settings::Units::Imperial));
}

// F5. The device said nothing when the receiver finally solved, and it
// transmitted the first solution it got. Both are wrong on the bench and in the
// air: a cold receiver's first fixes walk, and the pilot is left guessing.

// B4 + the low-battery warning: two things the status page is the only reader
// of. Both are drawn from the state the services publish, so this is the wiring
// test - ui/screens/status.cpp owns what they look like.
TEST_CASE("product: the status page carries the device's name and a cell that is low") {
    auto status_ink = [](const char* callsign, uint16_t millivolts, bool on_cable = false) {
        Rig rig;
        REQUIRE(rig.setup() == Status::Ok);
        rig.platform.battery().millivolts = millivolts;
        rig.platform.battery().external_power = on_cable;
        for (int i = 0; callsign[i] != 0 && i < 9; i++)
            rig.state().settings.callsign[i] = callsign[i];
        uint32_t t = 100;
        rig.press(t);  // radar -> six-pack
        rig.press(t);  // -> status
        // Long enough for the cutoff monitor to have made its mind up: it wants
        // three consecutive samples before it calls a cell low, and the page
        // draws what it decided rather than deciding again.
        rig.run(t, t + 8000);
        REQUIRE(rig.product.screen().page() == go::Page::Status);
        return rig.product.screen().framebuffer().count_black();
    };

    const int plain = status_ink("", 4050);
    CHECK(status_ink("D-KXYZ", 4050) > plain);
    // 3.45 V is under core/power's warning threshold: the row says LOW rather
    // than leaving a pilot to read the number and know what it means. The same
    // cell on the cable is charging, and nothing on a charger is low.
    const int low = status_ink("", 3450);
    CHECK(low != plain);
    CHECK(status_ink("", 3450, /*on_cable=*/true) != low);
}

// D3, the reporting half. The marker on the page is the cutoff monitor's own
// verdict, published on the bus: the page does not compare millivolts a second
// time, so it cannot disagree with the thing that can switch the device off.

// D3, the reporting half. The marker on the page is the cutoff monitor's own
// verdict, published on the bus: the page does not compare millivolts a second
// time, so it cannot disagree with the thing that can switch the device off.
TEST_CASE("product: the status page marks a low cell when the monitor says so, not before") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.platform.battery().millivolts = 3450;
    uint32_t t = 100;
    rig.press(t);  // radar -> six-pack
    rig.press(t);  // -> status
    REQUIRE(rig.product.screen().page() == go::Page::Status);

    // Two samples under the warning is not yet a low cell: a transmit burst
    // sags the rail for as long as it lasts, and the third sample is what
    // decides. The page says nothing while the monitor has not.
    rig.run(t, 2500);
    REQUIRE(rig.state().power_level != power::PowerLevel::Low);
    const int undecided = rig.product.screen().framebuffer().count_black();

    rig.run(2500, 6000);
    REQUIRE(rig.state().power_level == power::PowerLevel::Low);
    // The same voltage and the same state of charge, so the only thing that can
    // have changed on the glass is the marker.
    CHECK(rig.product.screen().framebuffer().count_black() > undecided);
}

// K: the page has to name which part answered, not only that one did. LilyGO
// ships two barometer addresses, five e-paper lots and two kinds of haptic
// against the same footprints, so "BARO PASS" on its own does not identify the
// device a bench is holding.
TEST_CASE("product: the self-test page carries what the probes found, not what was expected") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);

    const ui::BootPart* baro = nullptr;
    const ui::BootPart* haptic = nullptr;
    const ui::BootPart* radio = nullptr;
    for (int i = 0; i < go::kBootPartCount; i++) {
        const ui::BootPart& row = rig.product.boot_rows()[i];
        if (go::kBootParts[i].capability == hal::Capability::Baro) baro = &row;
        if (go::kBootParts[i].capability == hal::Capability::Vibro) haptic = &row;
        if (go::kBootParts[i].capability == hal::Capability::Rf) radio = &row;
    }
    REQUIRE(baro != nullptr);
    REQUIRE(haptic != nullptr);
    REQUIRE(radio != nullptr);

    // The host bus answers the BOM's address, and the page prints the address
    // that answered rather than the one in the devicetree.
    REQUIRE(baro->detail != nullptr);
    CHECK(std::string(baro->detail) == "76");
    // The haptic on this platform is the waveform driver, so the row says which.
    REQUIRE(haptic->detail != nullptr);
    CHECK(std::string(haptic->detail) == "DRV2605");
    // A footprint with only one part behind it gets no detail: the row is the row
    // it always was, and a second verdict is not smuggled in as a name.
    CHECK(radio->detail == nullptr);
}

// A barometer that did not answer must not print a stale or a zero address: the
// row already says ABSENT, and "00" would read as a part at address zero.
TEST_CASE("product: a footprint nothing answered prints no address") {
    constexpr hal::Capabilities kNoBaro = static_cast<hal::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(hal::Capability::Baro));
    Rig rig{kNoBaro};
    REQUIRE(rig.setup() == Status::Ok);

    for (int i = 0; i < go::kBootPartCount; i++) {
        if (go::kBootParts[i].capability != hal::Capability::Baro) continue;
        const ui::BootPart& row = rig.product.boot_rows()[i];
        CHECK(row.state == ui::PartState::Absent);
        CHECK(row.detail == nullptr);
    }
}
