#include "products/skyblip_go/services/screen.h"

#include <cstring>

#include "core/flight/atmosphere.h"
#include "core/power/cutoff.h"
#include "core/protocol/nmea_out.h"
#include "core/util/units.h"
#include "ui/widgets/wordmark.h"

namespace skyblip::go {

// SoftRF plays a six-note jingle on the very first fix
// (oss/SoftRF-lyusupov .../src/platform/nRF52.cpp:2767-2787); the moshe-braner
// fork adds a two-tone confirmation and then waits before it transmits. This is
// the confirmation half. It runs before the display checks below because a
// device with no panel fitted still owes the pilot the answer.
void ScreenService::annunciate_first_fix(uint32_t now_ms) {
    const bool quiet = context_.state.alarm_level == 0;
    if (fix_watch_.take_acquired()) {
        dirty_ = true;
        if (!context_.state.settings.alarm_enabled || !quiet) return;
        context_.roles.annunciator.alarm(kFirstFixToneLevel, context_.state.settings.alarm_volume);
        tone_since_ms_ = now_ms;
        tone_on_ = true;
        return;
    }
    if (!tone_on_ || now_ms - tone_since_ms_ < kFirstFixToneMs) return;
    tone_on_ = false;
    if (quiet) context_.roles.annunciator.silence();
}

void ScreenService::tick(uint32_t now_ms) {
    fix_watch_.update(context_.state.own.fix_valid, now_ms);
    annunciate_first_fix(now_ms);

    messages::ButtonEvent press{};
    while (context_.bus.input.pop(press)) next_page();

    if (context_.state.alarm_level != last_alarm_) {
        last_alarm_ = context_.state.alarm_level;
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
        if (context_.state.settings.page_mask & (1u << cand)) {
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
    // INFO: fc 01aug25 pushed before power-off: the glass wears it while off
    fb_.clear(/*white=*/true);
    ui::draw_wordmark(fb_, ui::Framebuffer::kW / 2, ui::Framebuffer::kH / 2);
    context_.roles.display.present(fb_, hal::Refresh::Full, last_render_ms_);
    context_.roles.display.power_off();
}

void ScreenService::render() {
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
            // 1 m/s = 1.94384 kt, from quarter-m/s.
            snap.speed_kt = (static_cast<int32_t>(own.speed_q) * 194384) / (4 * 100000);
            snap.alt_ft = to_feet(Metres(own.alt_m)).v;
            snap.vs_fpm = climb_fpm();
            snap.track_deg = to_degrees(Cordic9(own.track_c9)).v;
            snap.turn_dps = context_.state.turn_dps;
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
            // The acting side of this threshold is core/power's CutoffMonitor,
            // which debounces before it takes the device down. The page only
            // reports, and the gauge behind it is already a median of three.
            snap.battery_low = context_.state.battery.valid &&
                               !context_.state.battery.external_power &&
                               context_.state.battery.millivolts < power::kLowWarnMv;
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
