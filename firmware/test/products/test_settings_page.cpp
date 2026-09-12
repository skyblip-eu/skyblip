// The settings page through the whole product: presses in at the pin the board
// polls, the flash blob and the framebuffer out. Nothing below the services is
// stubbed, so a case here is what a pilot's thumb does to the unit.
//
// The gate this closes is G3: a pilot with no phone can change the aircraft
// type and the alarm volume. What matters as much as that they can is that
// doing it cannot spend the other two things the one button says - the double
// press that authorises a firmware upload, and the hold that switches the
// device off.
#include "core/settings/settings.h"
#include "core/timing/durable_write.h"
#include "doctest/doctest.h"
#include "test/support/product_rig.h"
#include "ui/input/gesture.h"
#include "ui/screens/confirm.h"
#include "ui/screens/settings.h"

using namespace skyblip;

namespace {

constexpr uint32_t kWindow = ui::ConfirmGesture::kDoublePressMs;

// Long enough for a press to have finished meaning one thing before the next
// one is made: the pairing window, and a little.
void settle(Rig& rig, uint32_t& t) {
    rig.run(t, t + kWindow + 200);
    t += kWindow + 200;
}

void push_fix(Rig& rig, uint16_t speed_q, int32_t alt_msl_m) {
    gnss::GnssFix fix{};
    fix.valid = true;
    fix.speed_q = speed_q;
    fix.alt_msl_m = alt_msl_m;
    fix.updates = 1;
    rig.product.bus().gnss.push(fix);
}

// core/flight decides what a fix stream means, and the companion link's gate
// reads what it decided. Standing still is on the ground; five seconds of
// believed motion is airborne.
void on_ground(Rig& rig, uint32_t& t) {
    push_fix(rig, 0, 0);
    rig.run(t, t + 200);
    t += 200;
}

void airborne(Rig& rig, uint32_t& t) {
    for (int i = 0; i < 14; i++) {
        push_fix(rig, 200, 1200);
        rig.run(t, t + 500);
        t += 500;
    }
}

void open_settings(Rig& rig, uint32_t& t) {
    rig.press(t);
    REQUIRE(rig.product.screen().mode() == go::Mode::Settings);
    settle(rig, t);
    REQUIRE(rig.product.screen().showing_self_test());
    rig.tap_pad(t);
    settle(rig, t);
}

// A tap of the pad: the focus moves down a row.
void move(Rig& rig, uint32_t& t) {
    rig.tap_pad(t);
    settle(rig, t);
}

// A press of the button: the focused row is acted on, once, and the focus stays on it.
void change(Rig& rig, uint32_t& t) {
    rig.press(t);
    rig.run(t, t + 200);
    t += 200;
    settle(rig, t);
}

// The blob goes to flash kSettleMs after the last tap, in the next free window.
void run_past_the_write_settle(Rig& rig, uint32_t& t) {
    const uint32_t ms = timing::DurableWriteWindow::kSettleMs + 1000;
    rig.run(t, t + ms);
    t += ms;
}

void focus_on(Rig& rig, uint32_t& t, ui::SettingsRow row) {
    for (int i = 0; i < ui::kSettingsRowCount; i++) {
        if (rig.product.screen().editor().focus() == row) break;
        move(rig, t);
    }
    REQUIRE(rig.product.screen().editor().focus() == row);
}

void hold_button(Rig& rig, uint32_t& t, uint32_t ms, bool down = true) {
    rig.platform.board_gpio().button_down = down;
    const uint32_t until = t + ms;
    for (; t <= until; t += 50) {
        rig.platform.clock().set_millis(t);
        rig.product.step(t);
    }
}

bool stored_settings(Rig& rig, settings::Settings& out) {
    uint8_t blob[64];
    size_t n = 0;
    if (rig.platform.kv().read("settings", blob, sizeof(blob), n) != Status::Ok) return false;
    return settings::from_blob(blob, n, out) == Status::Ok;
}

ui::Framebuffer expected_page(Rig& rig) {
    ui::SettingsSnapshot snapshot;
    snapshot.values.settings = rig.state().settings;
    snapshot.values.qnh_pa = rig.state().qnh_pa;
    snapshot.focus = rig.product.screen().editor().focus();
    ui::Framebuffer fb;
    ui::draw_settings(fb, snapshot);
    return fb;
}

}  // namespace

TEST_CASE("product: a press on any traffic page opens the settings mode") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    rig.tap_pad(t);
    REQUIRE(rig.product.screen().page() == go::Page::SixPack);

    rig.press(t);
    CHECK(rig.product.screen().mode() == go::Mode::Settings);
    CHECK(rig.product.screen().editor().active());

    // On the rows the pad still navigates: it walks them one at a time.
    rig.tap_pad(t);
    settle(rig, t);
    REQUIRE(rig.product.screen().editor().focus() == ui::SettingsRow::Identity);
    move(rig, t);
    CHECK(rig.product.screen().editor().focus() == ui::SettingsRow::AircraftType);

    // The button on the Leave row hands the glass back.
    focus_on(rig, t, ui::SettingsRow::Leave);
    change(rig, t);
    CHECK(rig.product.screen().mode() == go::Mode::Traffic);
    CHECK(rig.product.screen().page() == go::Page::Radar);
    CHECK_FALSE(rig.product.screen().editor().active());
}

// The pad's gestures are one hold apart from the stow, and this one takes the device off.
TEST_CASE("product: a hold the button joins switches the device off, it opens nothing") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    rig.run(t, t + 500);
    t += 500;

    rig.platform.board_gpio().pad_down = true;
    rig.platform.board_gpio().button_down = true;
    rig.run(t, t + power::kLongPressMs + 400);
    t += power::kLongPressMs + 400;

    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::Stow);
    CHECK(rig.product.screen().mode() == go::Mode::Traffic);
}

// Boot no longer flashes the self test, so the settings mode is the only way to it.
TEST_CASE("product: the settings mode opens on the self test and a press walks into the rows") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    rig.press(t);
    REQUIRE(rig.product.screen().mode() == go::Mode::Settings);
    settle(rig, t);
    rig.run(t, t + 4000);
    t += 4000;

    CHECK(rig.product.screen().showing_self_test());
    CHECK(std::memcmp(rig.product.screen().framebuffer().data(), rig.product.boot_page().data(),
                      ui::Framebuffer::kBytes) == 0);
    CHECK(rig.platform.chips().epd.framebuffer().count_black() ==
          rig.product.boot_page().count_black());

    rig.press(t);
    settle(rig, t);
    rig.run(t, t + 4000);
    t += 4000;
    CHECK_FALSE(rig.product.screen().showing_self_test());
    CHECK(rig.product.screen().editor().focus() == ui::SettingsRow::Identity);
    const ui::Framebuffer rows = expected_page(rig);
    CHECK(std::memcmp(rig.product.screen().framebuffer().data(), rows.data(),
                      ui::Framebuffer::kBytes) == 0);
}

TEST_CASE("product: the settings page reaches the glass, drawn from what the device is running") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    rig.state().settings.aircraft_type = 4;
    open_settings(rig, t);
    rig.run(t, t + 4000);
    t += 4000;

    const ui::Framebuffer expected = expected_page(rig);
    CHECK(std::memcmp(rig.product.screen().framebuffer().data(), expected.data(),
                      ui::Framebuffer::kBytes) == 0);
    // Not just in the buffer: on the panel, which is the only place a pilot
    // with no phone can read it.
    CHECK(rig.platform.chips().epd.framebuffer().count_black() == expected.count_black());
    CHECK(expected.count_black() > 200);
}

TEST_CASE("product: the alarm volume a pilot sets on the panel is the one that survives a boot") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    REQUIRE(rig.state().settings.alarm_volume == 3);

    open_settings(rig, t);
    focus_on(rig, t, ui::SettingsRow::Volume);
    change(rig, t);
    rig.run(t, t + 500);
    t += 500;
    CHECK(rig.state().settings.alarm_volume == 4);

    // The screen did not write the flash: it said the settings had changed and
    // the service that owns the blob wrote it, exactly as it does for a phone.
    settings::Settings stored{};
    REQUIRE(stored_settings(rig, stored));
    CHECK(stored.alarm_volume == 4);

    // And the blob is one a device coming up cold accepts as itself.
    Rig again;
    uint8_t blob[64];
    size_t n = 0;
    REQUIRE(rig.platform.kv().read("settings", blob, sizeof(blob), n) == Status::Ok);
    REQUIRE(again.platform.kv().write("settings", blob, n) == Status::Ok);
    REQUIRE(again.setup() == Status::Ok);
    CHECK(again.state().settings.alarm_volume == 4);
}

TEST_CASE("product: the aircraft type set on the panel is the one that goes on the air") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    REQUIRE(rig.state().settings.aircraft_type == 4);

    open_settings(rig, t);
    focus_on(rig, t, ui::SettingsRow::AircraftType);
    change(rig, t);
    change(rig, t);
    run_past_the_write_settle(rig, t);
    CHECK(rig.state().settings.aircraft_type == 6);

    settings::Settings stored{};
    REQUIRE(stored_settings(rig, stored));
    CHECK(stored.aircraft_type == 6);

    // The category the transmitter puts in the frame is own.aircraft_cat, and
    // the own-ship service copies it off the settings on the next fix: the page
    // does not need a second wire, and it must not grow one.
    push_fix(rig, 100, 500);
    rig.run(t, t + 1000);
    CHECK(rig.state().own.aircraft_cat == 6);
}

TEST_CASE("product: walking the rows without changing one writes nothing at all") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    const settings::Settings before = rig.state().settings;

    open_settings(rig, t);
    for (int i = 0; i < ui::kSettingsRowCount; i++) move(rig, t);

    // Off the last row and back to the traffic picture the pad pages through.
    CHECK(rig.product.screen().page() == go::Page::Radar);
    CHECK_FALSE(rig.product.screen().editor().active());
    CHECK(rig.state().settings.aircraft_type == before.aircraft_type);
    CHECK(rig.state().settings.alarm_volume == before.alarm_volume);
    CHECK(rig.state().settings.page_mask == before.page_mask);

    // Nothing was staged, so there was nothing to write: the flash has never
    // been touched and the device cannot be half edited.
    settings::Settings stored{};
    CHECK_FALSE(stored_settings(rig, stored));

    rig.tap_pad(t);
    CHECK(rig.product.screen().page() == go::Page::SixPack);
}

TEST_CASE("product: a page nobody presses gives the traffic picture back on its own") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    open_settings(rig, t);
    focus_on(rig, t, ui::SettingsRow::Volume);

    rig.run(t, t + ui::SettingsEditor::kIdleReturnMs + 2000);
    t += ui::SettingsEditor::kIdleReturnMs + 2000;
    CHECK(rig.product.screen().page() == go::Page::Radar);
    CHECK_FALSE(rig.product.screen().editor().active());
}

TEST_CASE("product: the settings mode is reachable whatever the page mask says") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    // Every traffic page hidden, and the mode that undoes it still one gesture away.
    rig.state().settings.page_mask = 0;
    open_settings(rig, t);

    focus_on(rig, t, ui::SettingsRow::Pages);
    change(rig, t);
    CHECK(rig.state().settings.page_mask == ui::kPageMaskAll);

    // ... and leaving now lands on the radar, because the mask says it exists.
    focus_on(rig, t, ui::SettingsRow::Leave);
    move(rig, t);
    CHECK(rig.product.screen().page() == go::Page::Radar);
}

TEST_CASE("product: an invalid setting is never written by the page that could not fix it") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    // Whatever got it there, this struct is one settings::validate refuses.
    rig.state().settings.aircraft_type = 200;
    REQUIRE(settings::validate(rig.state().settings) != Status::Ok);

    open_settings(rig, t);
    focus_on(rig, t, ui::SettingsRow::Volume);
    change(rig, t);
    change(rig, t);
    rig.run(t, t + 500);

    CHECK(rig.state().settings.alarm_volume == 3);
    settings::Settings stored{};
    CHECK_FALSE(stored_settings(rig, stored));
}

// The half of this the gate cares most about: editing must not be able to spend
// the gesture that authorises, and must not be able to reach the way out.

TEST_CASE("product: a prompt takes the page, and the taps already in flight cannot answer it") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    on_ground(rig, t);
    open_settings(rig, t);
    focus_on(rig, t, ui::SettingsRow::Volume);
    const uint8_t volume = rig.state().settings.alarm_volume;

    // A phone asks to overwrite the firmware while the pilot is tapping a value
    // up. The prompt takes the glass at once.
    rig.send("{\"cmd\":\"dfu\"}");
    rig.run(t, t + 300);
    t += 300;
    REQUIRE(rig.product.screen().prompt() == comms::Pending::Dfu);

    // The thumb has not caught up: it keeps tapping at the rhythm that was
    // stepping the volume, through the moment the question appears. Not one of
    // those taps authorises anything, and none of them reaches the value
    // either - a press stream that began before the question cannot answer it.
    for (int i = 0; i < 6; i++) {
        rig.press(t);
        CHECK_FALSE(rig.product.config().config().upload_allowed());
    }
    rig.run(t, t + 200);
    t += 200;
    CHECK(rig.product.config().config().pending() == comms::Pending::Dfu);
    CHECK_FALSE(rig.product.config().config().upload_allowed());
    CHECK(rig.state().settings.alarm_volume == volume);

    // The pilot stops, and the question is on the glass where it can be read.
    rig.run(t, t + 4000);
    t += 4000;
    ui::ConfirmSnapshot expect;
    expect.title = comms::pending_title(comms::Pending::Dfu);
    expect.detail = comms::pending_detail(comms::Pending::Dfu);
    expect.timeout_s = comms::kConfirmWindowMs / 1000;
    ui::Framebuffer prompt_page;
    ui::draw_confirm(prompt_page, expect);
    CHECK(std::memcmp(rig.product.screen().framebuffer().data(), prompt_page.data(),
                      ui::Framebuffer::kBytes) == 0);
    CHECK(rig.platform.chips().epd.framebuffer().count_black() == prompt_page.count_black());

    // Only now is it answerable, and only by the gesture: physical presence,
    // deliberately, after the reading.
    rig.press(t);
    rig.press(t);
    rig.run(t, t + 200);
    t += 200;
    CHECK(rig.product.config().config().upload_allowed());

    // Back on the rows, at the top: the pilot was reading something else in between.
    CHECK(rig.product.screen().mode() == go::Mode::Settings);
    CHECK(rig.product.screen().editor().focus() == ui::SettingsRow::Identity);
}

TEST_CASE("product: a long press in the middle of an edit still switches the device off") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    open_settings(rig, t);
    focus_on(rig, t, ui::SettingsRow::Volume);
    const uint8_t volume = rig.state().settings.alarm_volume;

    hold_button(rig, t, power::kLongPressMs + 300);
    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::LongPress);
    CHECK(rig.product.shutdown().phase() == power::ShutdownPhase::Parking);
    CHECK_FALSE(rig.product.screen().powered());

    // A hold makes no press edge at all, so the row the pilot was standing on is untouched.
    CHECK(rig.state().settings.alarm_volume == volume);
    settings::Settings stored{};
    CHECK_FALSE(stored_settings(rig, stored));
}

TEST_CASE("product: the volume can be turned up in the air, where a phone is refused") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    airborne(rig, t);
    REQUIRE(rig.product.config().config().flight_state() == comms::FlightState::Airborne);

    // The companion app cannot: an unattended phone rewriting the whole struct
    // mid-flight is what that rule is for.
    rig.send("{\"cmd\":\"set\",\"alarm_volume\":5}");
    rig.run(t, t + 300);
    t += 300;
    CHECK(rig.product.config().config().pending() == comms::Pending::None);
    CHECK(rig.state().settings.alarm_volume == 3);

    // The pilot, on the panel, can. Presence is the thing the phone lacks, and
    // an alarm that is too quiet under a headset is a thing you find out about
    // in the air.
    open_settings(rig, t);
    focus_on(rig, t, ui::SettingsRow::Volume);
    change(rig, t);
    change(rig, t);
    run_past_the_write_settle(rig, t);
    CHECK(rig.state().settings.alarm_volume == 5);

    settings::Settings stored{};
    REQUIRE(stored_settings(rig, stored));
    CHECK(stored.alarm_volume == 5);
}

// A full refresh flashes the panel for about 2.5 s, so a menu swap goes through black instead.
TEST_CASE("product: the page transitions through black, and no keypress asks for a full") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    rig.run(t, t + 3000);  // the boot frame is the full one, and it is the last
    t += 3000;
    REQUIRE(rig.platform.chips().epd.last_full);

    open_settings(rig, t);
    rig.run(t, t + 4000);
    t += 4000;
    CHECK_FALSE(rig.platform.chips().epd.last_full);
    CHECK(rig.platform.chips().epd.present_count >= 3);  // boot, the black frame, then the page

    focus_on(rig, t, ui::SettingsRow::Volume);
    change(rig, t);
    change(rig, t);
    rig.run(t, t + 2000);
    t += 2000;
    CHECK_FALSE(rig.platform.chips().epd.last_full);

    focus_on(rig, t, ui::SettingsRow::Leave);
    move(rig, t);
    REQUIRE(rig.product.screen().page() == go::Page::Radar);
    rig.run(t, t + 4000);
    CHECK_FALSE(rig.platform.chips().epd.last_full);
}
