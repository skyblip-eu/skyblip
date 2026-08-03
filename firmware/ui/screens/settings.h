// The settings page: the panel half of "a pilot with no phone can change the
// things that matter". It is a list of rows a thumb walks and a small editor
// that decides what a press means, both pure: the page takes a snapshot and the
// editor takes the values in and hands new values back, so the service owns the
// state and this file owns the meaning.
//
// One button already says three things (a press pages, two presses inside
// ConfirmGesture::kDoublePressMs authorise a standing prompt, a hold past
// power::kLongPressMs switches the device off) and this page must not take any
// of them away. So it borrows the shape rather than the meaning: on this page a
// press is held for the same double-press window before it is acted on, one
// press moves the focus down a row, two inside the window act on the focused
// row, and further presses inside the window act again so a value can be
// stepped by tapping. A hold is never a tap, so the way out of the device is
// untouched, and an authorisation prompt takes the button away from this page
// entirely before the gesture that answers it can be armed.
//
// A pilot cannot get stuck here: the rows only ever advance, the last one is
// the way back to the traffic page, and a page nobody has pressed for
// kIdleReturnMs shows the traffic again on its own.
#ifndef SKYBLIP_UI_SCREENS_SETTINGS_H
#define SKYBLIP_UI_SCREENS_SETTINGS_H

#include <cstdint>

#include "core/settings/settings.h"
#include "ui/framebuffer.h"
#include "ui/input/gesture.h"

namespace skyblip::ui {

// The rows, in the order the thumb meets them. Identity is first and cannot be
// changed, which is what makes the fastest possible pair of presses after
// landing on the page - the impatient thumb still cycling pages - harmless.
// Leave is last, so walking the list with single presses always ends outside it.
enum class SettingsRow : uint8_t {
    Identity,
    AircraftType,
    Alarm,
    Volume,
    Units,
    QnhUp,
    QnhDown,
    Pages,
    Leave,
    kCount
};

constexpr int kSettingsRowCount = static_cast<int>(SettingsRow::kCount);

// Everything this page may touch: the blob core/settings owns, plus the
// altimeter subscale, which lives on the bus because it belongs to the day and
// not to the device.
struct SettingsValues {
    settings::Settings settings{};
    uint32_t qnh_pa{0};
};

struct SettingsSnapshot {
    SettingsValues values{};
    SettingsRow focus{SettingsRow::Identity};
};

// A subscale is set in whole hectopascals and the range is the one an altimeter
// window turns through. Stepping stops at the ends rather than wrapping: 111
// values behind a wrap is not a thing a thumb should be able to fall off.
constexpr uint32_t kQnhStepPa = 100;
constexpr uint32_t kQnhMinPa = 94000;
constexpr uint32_t kQnhMaxPa = 105000;

constexpr uint8_t kMaxAlarmVolume = 5;

constexpr uint8_t kPageMaskAll = 0x0F;
constexpr uint8_t kPageMaskTrafficStatus = 0x05;
constexpr uint8_t kPageMaskTrafficOnly = 0x01;

// The categories of ADS-L 4 SRD-860 issue 2 G.1.4 that name an aircraft. The
// codes above this are reserved or a second UAV encoding, so the page does not
// offer them; a code the companion app stored is still shown, as a number.
constexpr uint8_t kNamedAircraftTypes = 12;

enum class SettingsAction : uint8_t { None, Moved, Changed, Leave };

const char* settings_row_label(SettingsRow row);
const char* aircraft_type_name(uint8_t code);
uint8_t next_aircraft_type(uint8_t code);
uint8_t next_page_mask(uint8_t mask);
uint32_t step_qnh_pa(uint32_t qnh_pa, bool up);

// Writes the row's value, NUL-terminated, and returns its length. The longest
// is "RADAR STATUS", so 16 bytes is enough for any of them.
constexpr int kSettingsValueCap = 16;
int settings_row_value(char* out, SettingsRow row, const SettingsValues& values);

// The grid, exported so a test reads a row back off the glass rather than
// counting ink.
constexpr int kSettingsLeftX = 6;
constexpr int kSettingsCellW = 6;
constexpr int kSettingsRightX = 194;
constexpr int kSettingsRowsTop = 24;
constexpr int kSettingsRowHeight = 18;
constexpr int kSettingsTextInset = 5;
constexpr int kSettingsHintY = 190;
constexpr const char* kSettingsHintText = "PRESS MOVES  TWICE CHANGES";

constexpr int settings_row_top(SettingsRow row) {
    return kSettingsRowsTop + static_cast<int>(row) * kSettingsRowHeight;
}
constexpr int settings_row_text_y(SettingsRow row) {
    return settings_row_top(row) + kSettingsTextInset;
}

void draw_settings(Framebuffer& fb, const SettingsSnapshot& snapshot);

// Which row is focused, what a press does, what commits and what abandons. It
// holds no settings of its own: an accepted change is handed straight back to
// the caller, already validated, so there is no draft to lose and no way to
// leave the device half edited. Nothing commits except an act on a row, and an
// act is one indivisible step of one field.
class SettingsEditor {
   public:
    // INFO: cf 02aug26 A page left open is the traffic picture taken away. A
    // minute of no presses is a pilot who stopped editing, whatever the reason,
    // so the page hands itself back rather than waiting to be dismissed.
    static constexpr uint32_t kIdleReturnMs = 60000;

    void enter(uint32_t now_ms);
    void leave();

    bool active() const { return active_; }
    SettingsRow focus() const { return focus_; }

    // One debounced press edge. Nothing happens here: what it meant is only
    // known once the pairing window has passed or a second press has arrived.
    void press(uint32_t now_ms);

    // Once per service step. Returns Changed with next filled in when a value
    // was accepted, Moved when the focus moved, Leave when the page is done
    // with the button, None otherwise.
    SettingsAction tick(uint32_t now_ms, const SettingsValues& current, SettingsValues& next);

   private:
    SettingsAction act(const SettingsValues& current, SettingsValues& next);
    SettingsAction advance();

    SettingsRow focus_{SettingsRow::Identity};
    uint32_t press_ms_{0};
    uint32_t act_ms_{0};
    uint32_t idle_since_ms_{0};
    bool active_{false};
    bool waiting_{false};
    bool acting_{false};
    bool repeating_{false};
};

}  // namespace skyblip::ui

#endif
