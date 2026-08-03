// The settings page and its editor, on their own. This is the file that says
// what the one button means once a pilot is standing on the page: which row is
// focused, what a press does, what commits and what abandons. It is all pure,
// so the rules can be read here rather than inferred from a service.
//
// The three things the button already said stay untouched. A hold is not a tap
// and never reaches this file; the authorising double press belongs to a prompt
// and a prompt takes the page away before it can be answered
// (test/products/test_settings_page.cpp holds that half); and the press that
// pages is the same press that moves a row here, at the same rhythm.
#include "core/flight/atmosphere.h"
#include "core/settings/settings.h"
#include "doctest/doctest.h"
#include "ui/framebuffer.h"
#include "ui/input/gesture.h"
#include "ui/screens/settings.h"

using namespace skyblip;
using namespace skyblip::ui;

namespace {

int length(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

// Draw the same text at the same place in a scratch buffer and compare the box
// it occupies, so a case claims "the row says GLIDER" rather than "there is ink".
bool reads_at(const Framebuffer& fb, int x, int y, const char* text, bool ink) {
    Framebuffer expected;
    expected.clear(true);
    if (!ink) expected.clear(false);
    expected.draw_text(x, y, text, ink, 1);
    for (int dy = 0; dy < 7; dy++)
        for (int dx = 0; dx < length(text) * kSettingsCellW; dx++)
            if (fb.get_pixel(x + dx, y + dy) != expected.get_pixel(x + dx, y + dy)) return false;
    return true;
}

bool row_label_reads(const Framebuffer& fb, SettingsRow row, bool focused) {
    return reads_at(fb, kSettingsLeftX, settings_row_text_y(row), settings_row_label(row),
                    !focused);
}

bool row_value_reads(const Framebuffer& fb, SettingsRow row, const char* value, bool focused) {
    return reads_at(fb, kSettingsRightX - length(value) * kSettingsCellW, settings_row_text_y(row),
                    value, !focused);
}

SettingsValues fresh() {
    SettingsValues v;
    v.settings = settings::defaults(0x3A7F2C);
    v.qnh_pa = flight::kIsaSeaLevelPa;
    return v;
}

Framebuffer page(const SettingsValues& values, SettingsRow focus) {
    SettingsSnapshot snapshot;
    snapshot.values = values;
    snapshot.focus = focus;
    Framebuffer fb;
    draw_settings(fb, snapshot);
    return fb;
}

// The editor with a clock the case advances, and the values it hands back
// applied, which is exactly what the service does with them.
struct Bench {
    SettingsEditor editor;
    SettingsValues values{fresh()};
    uint32_t t{1000};

    Bench() { editor.enter(t); }

    SettingsAction run(uint32_t ms) {
        SettingsAction seen = SettingsAction::None;
        for (uint32_t i = 0; i < ms; i += 10) {
            t += 10;
            SettingsValues next;
            const SettingsAction action = editor.tick(t, values, next);
            if (action == SettingsAction::Changed) values = next;
            if (action != SettingsAction::None) seen = action;
        }
        return seen;
    }

    // One deliberate press: the thumb goes down once and stops, so the pairing
    // window closes on it and it can only have meant "next row".
    SettingsAction single() {
        editor.press(t);
        return run(ConfirmGesture::kDoublePressMs + 100);
    }

    // Two presses inside the window: the gesture that acts on the focused row.
    SettingsAction pair() {
        editor.press(t);
        run(100);
        editor.press(t);
        return run(100);
    }

    // A third and fourth press at the same rhythm, each one another step of the
    // same field: this is what makes a subscale settable with a thumb.
    SettingsAction again() {
        editor.press(t);
        return run(100);
    }

    void focus_on(SettingsRow row) {
        for (int i = 0; i < kSettingsRowCount && editor.focus() != row; i++) single();
        REQUIRE(editor.focus() == row);
    }
};

}  // namespace

TEST_CASE("settings page: every row names what it holds, and the focused one is reversed out") {
    SettingsValues values = fresh();
    values.settings.aircraft_type = 4;
    values.settings.alarm_volume = 3;

    const Framebuffer fb = page(values, SettingsRow::Volume);

    CHECK(row_label_reads(fb, SettingsRow::Identity, false));
    CHECK(row_value_reads(fb, SettingsRow::Identity, "3A7F2C", false));
    CHECK(row_label_reads(fb, SettingsRow::AircraftType, false));
    CHECK(row_value_reads(fb, SettingsRow::AircraftType, "GLIDER", false));
    CHECK(row_label_reads(fb, SettingsRow::Alarm, false));
    CHECK(row_value_reads(fb, SettingsRow::Alarm, "ON", false));
    CHECK(row_value_reads(fb, SettingsRow::Units, "METRIC", false));
    CHECK(row_value_reads(fb, SettingsRow::QnhUp, "1013 HPA", false));
    CHECK(row_value_reads(fb, SettingsRow::Pages, "ALL", false));
    CHECK(row_label_reads(fb, SettingsRow::Leave, false));

    // The focused row is white ink on a filled bar: told apart by shape at
    // arm's length, before a word of it is read.
    CHECK(row_label_reads(fb, SettingsRow::Volume, true));
    CHECK(row_value_reads(fb, SettingsRow::Volume, "3 OF 5", true));
    CHECK_FALSE(row_label_reads(fb, SettingsRow::Volume, false));

    // Exactly one bar, and the page says how the button works.
    int bars = 0;
    for (int i = 0; i < kSettingsRowCount; i++) {
        const SettingsRow row = static_cast<SettingsRow>(i);
        if (row_label_reads(fb, row, true)) bars++;
    }
    CHECK(bars == 1);
    CHECK(reads_at(fb, kSettingsLeftX - 2, kSettingsHintY, kSettingsHintText, true));

    // And nothing falls off the bottom of a 200 pixel panel.
    CHECK(settings_row_top(SettingsRow::Leave) + kSettingsRowHeight <= kSettingsHintY);
    CHECK(kSettingsHintY + 7 < Framebuffer::kH);
}

TEST_CASE("settings page: a category the phone stored but the page does not name is still shown") {
    SettingsValues values = fresh();
    values.settings.aircraft_type = 13;
    const Framebuffer fb = page(values, SettingsRow::Identity);
    CHECK(row_value_reads(fb, SettingsRow::AircraftType, "TYPE 13", false));

    // ... and the first change moves it into the list the page can name, rather
    // than stepping deeper into the reserved codes.
    CHECK(next_aircraft_type(13) == 0);
}

TEST_CASE("settings editor: one press moves down a row, two change the row it is on") {
    Bench bench;
    CHECK(bench.editor.focus() == SettingsRow::Identity);

    CHECK(bench.single() == SettingsAction::Moved);
    CHECK(bench.editor.focus() == SettingsRow::AircraftType);
    CHECK(bench.single() == SettingsAction::Moved);
    CHECK(bench.editor.focus() == SettingsRow::Alarm);

    // The pair acts on the row the focus is on, not on the one after it: that
    // is the whole reason a press waits out the pairing window before it moves.
    CHECK(bench.values.settings.alarm_enabled);
    CHECK(bench.pair() == SettingsAction::Changed);
    CHECK(bench.editor.focus() == SettingsRow::Alarm);
    CHECK_FALSE(bench.values.settings.alarm_enabled);
}

TEST_CASE("settings editor: the row the page opens on cannot change anything") {
    // A pilot cycling pages fast lands here and taps again out of impatience.
    // The fastest pair of presses possible on this page falls on the identity
    // row, which is a thing to read and not a thing to set.
    Bench bench;
    const settings::Settings before = bench.values.settings;
    CHECK(bench.pair() == SettingsAction::None);
    CHECK(bench.editor.focus() == SettingsRow::Identity);
    CHECK(before.aircraft_type == bench.values.settings.aircraft_type);
    CHECK(before.alarm_volume == bench.values.settings.alarm_volume);
}

TEST_CASE("settings editor: the focus only ever advances, and walks out of the page") {
    Bench bench;
    for (int i = 1; i < kSettingsRowCount; i++) {
        CHECK(bench.single() == SettingsAction::Moved);
        CHECK(static_cast<int>(bench.editor.focus()) == i);
    }
    REQUIRE(bench.editor.focus() == SettingsRow::Leave);

    // One more press leaves. A thumb that only knows one press can never be
    // trapped on this page: pressing enough times always ends outside it.
    CHECK(bench.single() == SettingsAction::Leave);
    CHECK_FALSE(bench.editor.active());
    CHECK(bench.editor.focus() == SettingsRow::Identity);

    // The next entry starts at the top again, so the focus cycle is closed.
    bench.editor.enter(bench.t);
    CHECK(bench.editor.focus() == SettingsRow::Identity);
    CHECK(bench.editor.active());

    // And the pair works on the last row too, for a pilot who is done editing
    // and does not want to wait out a window.
    bench.focus_on(SettingsRow::Leave);
    CHECK(bench.pair() == SettingsAction::Leave);
    CHECK_FALSE(bench.editor.active());
}

TEST_CASE("settings editor: moving the focus over a row is not editing it") {
    // The abandoned edit. Walking the whole page, changing nothing, leaves the
    // settings byte for byte as they were: nothing is staged, so nothing is
    // half applied and there is nothing to lose by leaving.
    Bench bench;
    const settings::Settings before = bench.values.settings;
    const uint32_t qnh_before = bench.values.qnh_pa;
    for (int i = 0; i < kSettingsRowCount; i++) bench.single();
    CHECK_FALSE(bench.editor.active());
    CHECK(before.aircraft_type == bench.values.settings.aircraft_type);
    CHECK(before.alarm_enabled == bench.values.settings.alarm_enabled);
    CHECK(before.alarm_volume == bench.values.settings.alarm_volume);
    CHECK(before.units == bench.values.settings.units);
    CHECK(before.page_mask == bench.values.settings.page_mask);
    CHECK(qnh_before == bench.values.qnh_pa);
}

TEST_CASE("settings editor: aircraft type walks the categories that name an aircraft") {
    Bench bench;
    bench.focus_on(SettingsRow::AircraftType);
    REQUIRE(bench.values.settings.aircraft_type == 4);

    CHECK(bench.pair() == SettingsAction::Changed);
    CHECK(bench.values.settings.aircraft_type == 5);

    // Tapping on keeps stepping the same row rather than walking away from it.
    CHECK(bench.again() == SettingsAction::Changed);
    CHECK(bench.values.settings.aircraft_type == 6);
    CHECK(bench.editor.focus() == SettingsRow::AircraftType);

    // Every step is a code the page can name, and the list closes.
    for (int i = 0; i < kNamedAircraftTypes; i++) {
        CHECK(bench.values.settings.aircraft_type < kNamedAircraftTypes);
        CHECK(aircraft_type_name(bench.values.settings.aircraft_type)[0] != 0);
        CHECK(settings::validate(bench.values.settings) == Status::Ok);
        bench.again();
    }
    CHECK(bench.values.settings.aircraft_type == 6);
}

TEST_CASE("settings editor: the volume a pilot can hear, and it stays inside what is valid") {
    Bench bench;
    bench.focus_on(SettingsRow::Volume);
    REQUIRE(bench.values.settings.alarm_volume == 3);

    bench.pair();
    CHECK(bench.values.settings.alarm_volume == 4);
    bench.again();
    CHECK(bench.values.settings.alarm_volume == 5);

    // Off the top it comes back to silent rather than to a value the validator
    // would refuse, and the whole cycle is storable.
    bench.again();
    CHECK(bench.values.settings.alarm_volume == 0);
    for (int i = 0; i <= kMaxAlarmVolume + 1; i++) {
        CHECK(bench.values.settings.alarm_volume <= kMaxAlarmVolume);
        CHECK(settings::validate(bench.values.settings) == Status::Ok);
        bench.again();
    }
}

TEST_CASE("settings editor: a value that would not validate is never handed back") {
    // Whatever put it there, the struct in front of the editor is one the
    // validator refuses. The page is then a page that reads and does not write:
    // it will not hand back a blob nothing else in the firmware would accept.
    Bench bench;
    bench.values.settings.aircraft_type = 200;
    bench.focus_on(SettingsRow::Volume);
    REQUIRE(settings::validate(bench.values.settings) != Status::Ok);

    const uint8_t volume = bench.values.settings.alarm_volume;
    CHECK(bench.pair() == SettingsAction::None);
    CHECK(bench.values.settings.alarm_volume == volume);
    CHECK(bench.values.settings.aircraft_type == 200);
}

TEST_CASE("settings editor: the subscale steps in whole hectopascals and stops at the ends") {
    Bench bench;
    bench.focus_on(SettingsRow::QnhUp);

    // The standard setting is 1013.25 hPa, which is not a whole one. The first
    // step lands on the number the panel was already showing, plus one.
    REQUIRE(bench.values.qnh_pa == flight::kIsaSeaLevelPa);
    CHECK(bench.pair() == SettingsAction::Changed);
    CHECK(bench.values.qnh_pa == 101400);
    bench.again();
    CHECK(bench.values.qnh_pa == 101500);

    // Up to the end of the altimeter window and no further: the step stops
    // rather than wrapping a hundred hectopascals round to the other end.
    for (int i = 0; i < 200; i++) bench.again();
    CHECK(bench.values.qnh_pa == kQnhMaxPa);
    CHECK(bench.again() == SettingsAction::None);

    bench.focus_on(SettingsRow::QnhDown);
    CHECK(bench.pair() == SettingsAction::Changed);
    CHECK(bench.values.qnh_pa == kQnhMaxPa - kQnhStepPa);
    for (int i = 0; i < 200; i++) bench.again();
    CHECK(bench.values.qnh_pa == kQnhMinPa);
    CHECK(bench.again() == SettingsAction::None);
}

TEST_CASE("settings editor: the page set cycles through sets that always leave a page standing") {
    Bench bench;
    bench.focus_on(SettingsRow::Pages);
    REQUIRE(bench.values.settings.page_mask == kPageMaskAll);

    bench.pair();
    CHECK(bench.values.settings.page_mask == kPageMaskTrafficStatus);
    bench.again();
    CHECK(bench.values.settings.page_mask == kPageMaskTrafficOnly);
    bench.again();
    CHECK(bench.values.settings.page_mask == kPageMaskAll);

    // Every set on the cycle keeps the traffic picture, so decluttering cannot
    // end with a device showing nothing.
    CHECK((kPageMaskAll & 1u) != 0);
    CHECK((kPageMaskTrafficStatus & 1u) != 0);
    CHECK((kPageMaskTrafficOnly & 1u) != 0);

    // A mask a companion app invented is not one of the three, so it is shown
    // as CUSTOM and the first change brings it back onto the cycle.
    CHECK(next_page_mask(0x08) == kPageMaskAll);
}

TEST_CASE("settings editor: a page nobody is pressing hands the traffic picture back") {
    Bench bench;
    bench.single();
    REQUIRE(bench.editor.active());

    CHECK(bench.run(SettingsEditor::kIdleReturnMs - 1000) == SettingsAction::None);
    CHECK(bench.editor.active());
    CHECK(bench.run(2000) == SettingsAction::Leave);
    CHECK_FALSE(bench.editor.active());

    // A press resets the clock, so a pilot working through the rows is never
    // dropped out from under their thumb.
    bench.editor.enter(bench.t);
    for (int i = 0; i < 3; i++) {
        bench.run(SettingsEditor::kIdleReturnMs - 5000);
        bench.editor.press(bench.t);
        bench.run(100);
    }
    CHECK(bench.editor.active());
}
