#include "ui/screens/settings.h"

#include "core/util/format.h"

namespace skyblip::ui {

namespace {

constexpr int kHeaderY = 3;
constexpr int kHeaderRuleY = 21;

int length(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

// INFO: cf 02aug26 ADS-L 4 SRD-860 issue 2 G.1.4, the wire values, so nothing
// has to map: the code stored is the code transmitted. 12 to 17 are a second
// UAV encoding and reserved, and a page that offers "reserved" as a choice is a
// page that invites a pilot to say nothing about themselves.
const char* const kAircraftNames[kNamedAircraftTypes] = {
    "UNKNOWN",    "LIGHT",      "HEAVY",    "HELICOPTER", "GLIDER",     "BALLOON",
    "MICROLIGHT", "PARAGLIDER", "SKYDIVER", "VTOL",       "GYROCOPTER", "UAV"};

void row_text(Framebuffer& fb, SettingsRow row, const char* label, const char* value,
              bool focused) {
    const int top = settings_row_top(row);
    const int y = settings_row_text_y(row);
    if (focused) fb.rect(2, top, Framebuffer::kW - 4, kSettingsRowHeight - 1, true, /*fill=*/true);
    const bool ink = !focused;
    fb.draw_text(kSettingsLeftX, y, label, ink, 1);
    fb.draw_text(kSettingsRightX - length(value) * kSettingsCellW, y, value, ink, 1);
}

}  // namespace

const char* settings_row_label(SettingsRow row) {
    switch (row) {
        case SettingsRow::Identity: return "ID";
        case SettingsRow::AircraftType: return "AIRCRAFT";
        case SettingsRow::Alarm: return "ALARM";
        case SettingsRow::Volume: return "VOLUME";
        case SettingsRow::Units: return "UNITS";
        case SettingsRow::QnhUp: return "QNH UP";
        case SettingsRow::QnhDown: return "QNH DOWN";
        case SettingsRow::Pages: return "PAGES";
        case SettingsRow::Leave: return "BACK TO TRAFFIC";
        default: return "";
    }
}

const char* aircraft_type_name(uint8_t code) {
    return code < kNamedAircraftTypes ? kAircraftNames[code] : "";
}

uint8_t next_aircraft_type(uint8_t code) {
    if (code + 1 >= kNamedAircraftTypes) return 0;
    return static_cast<uint8_t>(code + 1);
}

uint8_t next_page_mask(uint8_t mask) {
    if (mask == kPageMaskAll) return kPageMaskTrafficStatus;
    if (mask == kPageMaskTrafficStatus) return kPageMaskTrafficOnly;
    return kPageMaskAll;
}

uint32_t step_qnh_pa(uint32_t qnh_pa, bool up) {
    uint32_t whole = ((qnh_pa + kQnhStepPa / 2) / kQnhStepPa) * kQnhStepPa;
    if (whole < kQnhMinPa) whole = kQnhMinPa;
    if (whole > kQnhMaxPa) whole = kQnhMaxPa;
    if (up) return whole + kQnhStepPa > kQnhMaxPa ? kQnhMaxPa : whole + kQnhStepPa;
    return whole < kQnhMinPa + kQnhStepPa ? kQnhMinPa : whole - kQnhStepPa;
}

int settings_row_value(char* out, SettingsRow row, const SettingsValues& v) {
    int n = 0;
    switch (row) {
        case SettingsRow::Identity: n = fmt_hex(out, v.settings.device_addr, 6); break;
        case SettingsRow::AircraftType: {
            const char* name = aircraft_type_name(v.settings.aircraft_type);
            if (name[0] != 0) {
                n = fmt_string(out, name);
            } else {
                n = fmt_string(out, "TYPE ");
                n += fmt_uint(out + n, v.settings.aircraft_type);
            }
            break;
        }
        case SettingsRow::Alarm:
            n = fmt_string(out, v.settings.alarm_enabled ? "ON" : "OFF");
            break;
        case SettingsRow::Volume:
            n = fmt_uint(out, v.settings.alarm_volume);
            n += fmt_string(out + n, " OF 5");
            break;
        case SettingsRow::Units:
            n = fmt_string(out, v.settings.units == settings::Units::Metric ? "METRIC" : "FEET KT");
            break;
        case SettingsRow::QnhUp:
        case SettingsRow::QnhDown:
            n = fmt_uint(out, (v.qnh_pa + kQnhStepPa / 2) / kQnhStepPa);
            n += fmt_string(out + n, " HPA");
            break;
        case SettingsRow::Pages:
            if (v.settings.page_mask == kPageMaskAll)
                n = fmt_string(out, "ALL");
            else if (v.settings.page_mask == kPageMaskTrafficStatus)
                n = fmt_string(out, "RADAR STATUS");
            else if (v.settings.page_mask == kPageMaskTrafficOnly)
                n = fmt_string(out, "RADAR ONLY");
            else
                n = fmt_string(out, "CUSTOM");
            break;
        case SettingsRow::Leave:
        default: break;
    }
    out[n] = 0;
    return n;
}

void draw_settings(Framebuffer& fb, const SettingsSnapshot& s) {
    fb.clear(true);
    fb.draw_text(kSettingsLeftX - 2, kHeaderY, "SETTINGS", true, 2);
    fb.hline(kSettingsLeftX - 2, kHeaderRuleY, Framebuffer::kW - 2 * (kSettingsLeftX - 2), true);

    char value[kSettingsValueCap];
    for (int i = 0; i < kSettingsRowCount; i++) {
        const SettingsRow row = static_cast<SettingsRow>(i);
        settings_row_value(value, row, s.values);
        row_text(fb, row, settings_row_label(row), value, row == s.focus);
    }

    fb.draw_text(kSettingsLeftX - 2, kSettingsHintY, kSettingsHintText, true, 1);
}

void SettingsEditor::enter(uint32_t now_ms) {
    focus_ = SettingsRow::Identity;
    press_ms_ = now_ms;
    act_ms_ = now_ms;
    idle_since_ms_ = now_ms;
    active_ = true;
    waiting_ = false;
    acting_ = false;
    repeating_ = false;
}

void SettingsEditor::leave() {
    focus_ = SettingsRow::Identity;
    active_ = false;
    waiting_ = false;
    acting_ = false;
    repeating_ = false;
}

// INFO: cf 02aug26 The pairing window is the confirmation gesture's own, so the
// two things a thumb can say keep the same rhythm wherever it says them, and
// nothing here can be produced by the hold that powers the device down.
void SettingsEditor::press(uint32_t now_ms) {
    if (!active_) return;
    idle_since_ms_ = now_ms;

    if (repeating_ && now_ms - act_ms_ < ConfirmGesture::kDoublePressMs) {
        acting_ = true;
        act_ms_ = now_ms;
        return;
    }
    if (waiting_ && now_ms - press_ms_ < ConfirmGesture::kDoublePressMs) {
        waiting_ = false;
        acting_ = true;
        act_ms_ = now_ms;
        return;
    }
    waiting_ = true;
    repeating_ = false;
    press_ms_ = now_ms;
}

SettingsAction SettingsEditor::tick(uint32_t now_ms, const SettingsValues& current,
                                    SettingsValues& next) {
    if (!active_) return SettingsAction::None;
    if (acting_) {
        acting_ = false;
        repeating_ = true;
        return act(current, next);
    }
    if (waiting_ && now_ms - press_ms_ >= ConfirmGesture::kDoublePressMs) {
        waiting_ = false;
        repeating_ = false;
        return advance();
    }
    if (now_ms - idle_since_ms_ >= kIdleReturnMs) {
        leave();
        return SettingsAction::Leave;
    }
    return SettingsAction::None;
}

SettingsAction SettingsEditor::advance() {
    const int next_row = static_cast<int>(focus_) + 1;
    if (next_row >= kSettingsRowCount) {
        leave();
        return SettingsAction::Leave;
    }
    focus_ = static_cast<SettingsRow>(next_row);
    return SettingsAction::Moved;
}

// INFO: cf 02aug26 One gate for every accepted value, and it is the same
// settings::validate the companion link's JSON goes through: a page cannot
// store what a phone would be refused. The subscale is not in the blob, so it
// carries its own bounds, and a step that hit an end changes nothing at all.
SettingsAction SettingsEditor::act(const SettingsValues& current, SettingsValues& next) {
    next = current;
    switch (focus_) {
        case SettingsRow::Identity: return SettingsAction::None;
        case SettingsRow::AircraftType:
            next.settings.aircraft_type = next_aircraft_type(current.settings.aircraft_type);
            break;
        case SettingsRow::Alarm:
            next.settings.alarm_enabled = !current.settings.alarm_enabled;
            break;
        case SettingsRow::Volume:
            next.settings.alarm_volume =
                static_cast<uint8_t>((current.settings.alarm_volume + 1) % (kMaxAlarmVolume + 1));
            break;
        case SettingsRow::Units:
            next.settings.units = current.settings.units == settings::Units::Metric
                                      ? settings::Units::Imperial
                                      : settings::Units::Metric;
            break;
        case SettingsRow::QnhUp:
        case SettingsRow::QnhDown: {
            const uint32_t stepped = step_qnh_pa(current.qnh_pa, focus_ == SettingsRow::QnhUp);
            if (stepped == current.qnh_pa) return SettingsAction::None;
            next.qnh_pa = stepped;
            break;
        }
        case SettingsRow::Pages:
            next.settings.page_mask = next_page_mask(current.settings.page_mask);
            break;
        case SettingsRow::Leave:
        default: leave(); return SettingsAction::Leave;
    }

    if (settings::validate(next.settings) != Status::Ok || next.qnh_pa < kQnhMinPa ||
        next.qnh_pa > kQnhMaxPa) {
        next = current;
        return SettingsAction::None;
    }
    return SettingsAction::Changed;
}

}  // namespace skyblip::ui
