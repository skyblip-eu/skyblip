#include "products/skyblip_go/services/screen.h"

#include "core/protocol/nmea_out.h"
#include "core/util/units.h"

namespace skyblip::go {

void ScreenService::tick(uint32_t now_ms) {
    messages::ButtonEvent press{};
    while (context_.bus.input.pop(press)) next_page();

    if (context_.state.alarm_level != last_alarm_) {
        last_alarm_ = context_.state.alarm_level;
        dirty_ = true;
    }

    if (!hal::has(context_.roles.capabilities, hal::Capability::Display)) return;
    if (!dirty_ && now_ms - last_render_ms_ < kRenderPeriodMs) return;
    last_render_ms_ = now_ms;
    render();
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
}

void ScreenService::set_backlight(bool on) {
    backlight_ = on;
    context_.roles.display.set_backlight(on);
}

void ScreenService::set_power(bool on) {
    if (on)
        context_.roles.display.power_on();
    else
        context_.roles.display.power_off();
    dirty_ = true;
}

void ScreenService::render() {
    const bool full = dirty_;
    dirty_ = false;
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
        case Page::AltVs: {
            ui::AltVsSnapshot snap;
            snap.have_data = own.fix_valid;
            snap.imperial = settings.units == settings::Units::Imperial;
            snap.alt_ft = to_feet(Metres(own.alt_m)).v;
            snap.vs_fpm = climb_fpm();
            ui::draw_altvs(fb_, snap);
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
        case Page::Status:
        default: {
            ui::StatusSnapshot snap;
            snap.device_addr = settings.device_addr;
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
            snap.imperial = settings.units == settings::Units::Imperial;
            ui::draw_status(fb_, snap);
            break;
        }
    }

    context_.roles.display.present(fb_, {0, 0, ui::Framebuffer::kW, ui::Framebuffer::kH},
                                   full ? hal::Refresh::Full : hal::Refresh::Partial);
}

}  // namespace skyblip::go
