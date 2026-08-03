#include "products/skyblip_go/services/screen.h"

#include <cstring>

#include "core/flight/atmosphere.h"
#include "core/power/cutoff.h"
#include "core/protocol/nmea_out.h"
#include "core/util/units.h"
#include "ui/widgets/wordmark.h"

namespace skyblip::go {

// INFO: cf 02aug26 One press means one thing at a time. While a prompt stands
// there is no page cycling at all, so the press a pilot makes to change pages
// cannot be spent on an authorisation - and a lone press at a prompt refuses it
// rather than doing nothing, which is the same press failing closed.
void ScreenService::handle_input(uint32_t now_ms) {
    const comms::Pending pending = config_ ? config_->pending() : comms::Pending::None;
    if (pending != prompt_) {
        prompt_ = pending;
        dirty_ = true;
        want_full_ = true;
        gesture_.disarm();
        prompt_on_glass_ = false;
    }
    if (prompt_ != comms::Pending::None && !gesture_.armed()) {
        // INFO: cf 02aug26 The two conditions that make a press an answer
        // rather than an accident: the question has reached the glass where it
        // can be read, and the thumb has stopped. A run of presses that began
        // before the prompt - pages being cycled, or a value being stepped on
        // the settings page - keeps the gesture disarmed until it ends, so
        // nothing already in flight can be spent on an authorisation. With no
        // panel fitted there is nothing to read and presence is all there is.
        const bool readable =
            prompt_on_glass_ || !hal::has(context_.roles.capabilities, hal::Capability::Display);
        const bool quiet =
            !pressed_once_ || now_ms - last_press_ms_ >= ui::ConfirmGesture::kDoublePressMs;
        if (readable && quiet) gesture_.arm(now_ms);
    }

    sync_editor(now_ms);

    messages::ButtonEvent press{};
    while (context_.bus.input.pop(press)) {
        last_press_ms_ = now_ms;
        pressed_once_ = true;
        if (prompt_ != comms::Pending::None) {
            if (gesture_.armed()) resolve(gesture_.press(now_ms));
            continue;
        }
        if (editor_.active()) {
            editor_.press(now_ms);
            continue;
        }
        next_page();
        sync_editor(now_ms);
    }

    if (prompt_ != comms::Pending::None) {
        resolve(gesture_.tick(now_ms));
        return;
    }
    step_editor(now_ms);
}

// INFO: cf 02aug26 The settings page owns the button for as long as it is the
// page on the glass and nothing is being authorised. A prompt takes it away
// without asking, which is what makes "a standing prompt wins" true of the
// button as well as of the ink.
void ScreenService::sync_editor(uint32_t now_ms) {
    const bool wanted = page_ == Page::Settings && prompt_ == comms::Pending::None;
    if (wanted == editor_.active()) return;
    if (wanted)
        editor_.enter(now_ms);
    else
        editor_.leave();
}

void ScreenService::step_editor(uint32_t now_ms) {
    if (!editor_.active()) return;

    ui::SettingsValues current;
    current.settings = context_.state.settings;
    current.qnh_pa = context_.state.qnh_pa;
    ui::SettingsValues next;

    switch (editor_.tick(now_ms, current, next)) {
        case ui::SettingsAction::Changed:
            context_.state.settings = next.settings;
            context_.state.qnh_pa = next.qnh_pa;
            // INFO: cf 02aug26 One owner of the flash blob. The page changes the
            // struct the config service was already given a reference to and
            // says so with the same flag the companion link raises; the write
            // itself stays in go::ConfigLinkService::persist, so there is never
            // a second writer and never two versions of the blob.
            if (config_ != nullptr) config_->note_settings_changed();
            dirty_ = true;
            break;
        case ui::SettingsAction::Moved: dirty_ = true; break;
        case ui::SettingsAction::Leave:
            page_ = traffic_page();
            dirty_ = true;
            want_full_ = true;
            break;
        case ui::SettingsAction::None:
        default: break;
    }
}

// INFO: cf 02aug26 Where the settings page hands the glass back: the first page
// the mask leaves standing, which is the traffic picture unless a pilot hid it.
Page ScreenService::traffic_page() const {
    const uint8_t mask = context_.state.settings.page_mask;
    for (int i = 0; i < static_cast<int>(Page::Settings); i++)
        if (mask & (1u << i)) return static_cast<Page>(i);
    return Page::Radar;
}

void ScreenService::resolve(ui::Gesture gesture) {
    if (gesture == ui::Gesture::None) return;
    if (gesture == ui::Gesture::Confirm)
        config_->confirm();
    else
        config_->cancel();
    prompt_ = comms::Pending::None;
    gesture_.disarm();
    prompt_on_glass_ = false;
    dirty_ = true;
    want_full_ = true;
}

void ScreenService::tick(uint32_t now_ms) {
    // The chirp that goes with it belongs to the alarm service, which is the
    // one owner of the annunciator. This is the panel's half: a first fix
    // changes every page there is.
    if (context_.state.own.fix_acquired) dirty_ = true;
    handle_input(now_ms);

    if (context_.state.alarm_level != last_alarm_) {
        last_alarm_ = context_.state.alarm_level;
        dirty_ = true;
    }

    if (page_ == Page::Settings && context_.state.alarm_level >= kAlarmTakesGlass) {
        editor_.leave();
        page_ = traffic_page();
        dirty_ = true;
    }

    if (!hal::has(context_.roles.capabilities, hal::Capability::Display)) return;
    if (!powered_) return;

    const bool quiet = context_.state.alarm_level == 0 && context_.state.traffic.count() == 0;
    if (!quiet) quiet_since_ms_ = now_ms;

    if (!dirty_ && now_ms - last_render_ms_ < kRenderPeriodMs) return;
    if (!context_.roles.display.ready(now_ms)) return;
    if (presented_once_ && now_ms - last_present_ms_ < kPresentFloorMs) return;

    last_render_ms_ = now_ms;
    dirty_ = false;
    render();

    const bool changed = !presented_once_ ||
                         std::memcmp(fb_.data(), presented_.data(), ui::Framebuffer::kBytes) != 0;
    const bool full = decide_full(now_ms, quiet);
    if (!changed && !full) return;

    context_.roles.display.present(fb_, full ? hal::Refresh::Full : hal::Refresh::Fast, now_ms);
    note_presented(full ? hal::Refresh::Full : hal::Refresh::Fast, now_ms);
}

// INFO: fc 01aug25 a full refresh flashes ~2.5 s: pay ghost debt around the
// traffic picture, never in a pilot's face
bool ScreenService::decide_full(uint32_t now_ms, bool quiet) const {
    if (!presented_once_) return true;
    if (fasts_since_full_ >= kFastHardCeiling) return true;
    if (context_.state.alarm_level > 0) return false;
    if (want_full_ || fasts_since_full_ >= kFastPerFull) return true;
    if (kFullEveryMs != 0 && now_ms - last_full_ms_ >= kFullEveryMs) return true;
    return fasts_since_full_ > 0 && quiet && now_ms - quiet_since_ms_ >= kSkyEmptyBeforeFullMs;
}

void ScreenService::note_presented(hal::Refresh mode, uint32_t now_ms) {
    std::memcpy(presented_.data(), fb_.data(), ui::Framebuffer::kBytes);
    presented_once_ = true;
    prompt_on_glass_ = prompt_ != comms::Pending::None;
    last_present_ms_ = now_ms;
    if (mode == hal::Refresh::Full) {
        fasts_since_full_ = 0;
        want_full_ = false;
        last_full_ms_ = now_ms;
    } else {
        fasts_since_full_++;
    }
}

void ScreenService::next_page() {
    const int n = static_cast<int>(Page::kCount);
    for (int i = 1; i <= n; i++) {
        const int cand = (static_cast<int>(page_) + i) % n;
        const bool always = cand == static_cast<int>(Page::Settings);
        if (always || (context_.state.settings.page_mask & (1u << cand))) {
            page_ = static_cast<Page>(cand);
            break;
        }
    }
    dirty_ = true;
    want_full_ = true;
}

void ScreenService::set_backlight(bool on) {
    backlight_ = on;
    context_.roles.display.set_backlight(on);
}

void ScreenService::set_power(bool on) {
    dirty_ = true;
    want_full_ = true;
    powered_ = on;
    if (on) {
        context_.roles.display.power_on();
        return;
    }
    // A lit backlight is a rail nobody switched off: the panel sleeps, the LED
    // would not have.
    set_backlight(false);
    // INFO: fc 01aug25 pushed before power-off: the glass wears it while off
    fb_.clear(/*white=*/true);
    ui::draw_wordmark(fb_, ui::Framebuffer::kW / 2, ui::Framebuffer::kH / 2);
    context_.roles.display.present(fb_, hal::Refresh::Full, last_render_ms_);
    context_.roles.display.power_off();
}

void ScreenService::draw_prompt() {
    ui::ConfirmSnapshot snapshot;
    snapshot.title = comms::pending_title(prompt_);
    snapshot.detail = comms::pending_detail(prompt_);
    snapshot.timeout_s = comms::kConfirmWindowMs / 1000;
    ui::draw_confirm(fb_, snapshot);
}

void ScreenService::draw_settings_page() {
    ui::SettingsSnapshot snapshot;
    snapshot.values.settings = context_.state.settings;
    snapshot.values.qnh_pa = context_.state.qnh_pa;
    snapshot.focus = editor_.focus();
    ui::draw_settings(fb_, snapshot);
}

void ScreenService::render() {
    if (prompt_ != comms::Pending::None) {
        draw_prompt();
        return;
    }
    if (page_ == Page::Settings) {
        draw_settings_page();
        return;
    }

    fb_.clear(/*white=*/true);

    const messages::OwnState& own = context_.state.own;
    const settings::Settings& settings = context_.state.settings;

    switch (page_) {
        case Page::Radar: {
            ui::RadarSnapshot snap;
            snap.have_fix = own.fix_valid;
            snap.range_m = range_m_;
            snap.max_alarm = context_.state.alarm_level;
            snap.coverage = context_.state.clock.utc_valid;
            int n = 0;
            if (own.fix_valid) {
                for (int i = 0; i < traffic::TrafficTable::kCapacity && n < kMaxRadarTargets; i++) {
                    const traffic::Target* t = context_.state.traffic.at(i);
                    if (!t || !t->used) continue;
                    int32_t north = 0, east = 0, up = 0;
                    if (!protocol::relative_ned(own, t->obs, north, east, up)) continue;
                    targets_[n].north_m = north;
                    targets_[n].east_m = east;
                    targets_[n].up_m = up;
                    targets_[n].alarm_level = t->alarm_level;
                    n++;
                }
            }
            snap.n_targets = n;
            snap.targets = targets_;
            ui::draw_radar(fb_, snap);
            break;
        }
        case Page::SixPack: {
            ui::SixPackSnapshot snap;
            snap.have_data = own.fix_valid;
            snap.units = settings.units;
            // 1 m/s = 1.94384 kt, from quarter-m/s.
            snap.speed_kt = (static_cast<int32_t>(own.speed_q) * 194384) / (4 * 100000);
            snap.alt_ft = to_feet(Metres(own.alt_m)).v;
            snap.vs_fpm = climb_fpm();
            snap.track_deg = to_degrees(Cordic9(own.track_c9)).v;
            snap.turn_dps = own.turn_dps;
            ui::draw_sixpack(fb_, snap);
            break;
        }
        case Page::Signal: {
            ui::SignalSnapshot snap;
            snap.have_fix = own.fix_valid;
            snap.n_heard = context_.state.traffic.count();
            snap.n_rows =
                traffic::rank_by_range(context_.state.traffic, own, signal_rows_, ui::kSignalRows);
            snap.rows = signal_rows_;
            ui::draw_signal(fb_, snap);
            break;
        }
        case Page::Status:
        default: {
            ui::StatusSnapshot snap;
            snap.device_addr = settings.device_addr;
            snap.callsign = settings.callsign;
            snap.fix_valid = own.fix_valid;
            snap.utc_valid = own.utc_valid;
            snap.pps_locked = context_.state.clock.pps_locked;
            snap.sats = own.sats;
            snap.lat_1e7 = own.lat_1e7;
            snap.lon_1e7 = own.lon_1e7;
            snap.alt_m = own.alt_m;
            snap.speed_q = own.speed_q;
            snap.track_c9 = own.track_c9;
            snap.climb_e8 = own.climb_e8;
            snap.utc = own.utc;
            snap.n_targets = context_.state.traffic.count();
            snap.baro_valid = context_.state.baro_active;
            snap.battery_valid = context_.state.battery.valid;
            snap.battery_mv = context_.state.battery.millivolts;
            snap.battery_percent = context_.state.battery.percent;
            snap.charging = context_.state.battery.charging;
            // The decision belongs to core/power's CutoffMonitor, which has
            // already debounced it, ignored a cell on the cable and thrown out a
            // floating sense. The page reports what it decided.
            const power::PowerLevel level = context_.state.power_level;
            snap.battery_low =
                level == power::PowerLevel::Low || level == power::PowerLevel::Cutoff;
            snap.pressure_pa = context_.state.pressure_pa;
            snap.qnh_pa = context_.state.qnh_pa;
            if (context_.state.baro_active) {
                snap.alt_qnh_m =
                    flight::alt_cm_on_setting(context_.state.pressure_pa, context_.state.qnh_pa) /
                    100;
                snap.alt_std_m = flight::pressure_to_alt_cm(context_.state.pressure_pa) / 100;
            }
            ui::draw_status(fb_, snap);
            break;
        }
    }
}

}  // namespace skyblip::go
