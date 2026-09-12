#ifndef SKYBLIP_UI_SCREENS_SETTINGS_H
#define SKYBLIP_UI_SCREENS_SETTINGS_H

#include <cstdint>

#include "core/settings/settings.h"
#include "ui/framebuffer.h"

namespace skyblip::ui {

// INFO: cf 02aug26 Identity is first and unchangeable, so the first pair of presses is harmless
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

constexpr uint8_t kPageMaskAll = 0x1F;
constexpr uint8_t kPageMaskTrafficStatus = 0x05;
constexpr uint8_t kPageMaskTrafficOnly = 0x01;

// INFO: fc 12sep26 ADS-L G.1.4 codes 11 up are UAV and reserved, nothing a pilot sits in
constexpr uint8_t kNamedAircraftTypes = 11;

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
constexpr const char* kSettingsHintText = "PAD MOVES  PRESS CHANGES";

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

    void change(uint32_t now_ms);
    void next_row(uint32_t now_ms);

    // Once per service step. Returns Changed with next filled in when a value
    // was accepted, Moved when the focus moved, Leave when the page is done
    // with the button, None otherwise.
    SettingsAction tick(uint32_t now_ms, const SettingsValues& current, SettingsValues& next);

   private:
    SettingsAction act(const SettingsValues& current, SettingsValues& next);
    SettingsAction advance();

    enum class Pending : uint8_t { None, Act, Advance };

    SettingsRow focus_{SettingsRow::Identity};
    uint32_t idle_since_ms_{0};
    Pending pending_{Pending::None};
    bool active_{false};
};

}  // namespace skyblip::ui

#endif
