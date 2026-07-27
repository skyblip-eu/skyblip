// products/skyblip_go/app.cpp — see app.h. Pure C++17; no framework headers.
#include "products/skyblip_go/app.h"

#include "core/protocol/adsl.h"
#include "core/protocol/nmea_out.h"
#include "core/traffic/alarm.h"
#include "core/util/units.h"
#include "products/common/tasks.h"

namespace skyblip::go {

App::App(const Ports& ports) : p_(ports), config_(ports.link, settings_, ports.dfu) {}

void App::load_settings() {
    settings_ = settings::defaults(p_.device_addr);
    if (!p_.kv) return;
    uint8_t blob[64];
    size_t n = 0;
    if (!is_ok(p_.kv->read("settings", blob, sizeof(blob), n))) return;
    settings::Settings loaded;
    if (is_ok(settings::from_blob(blob, n, loaded)) && is_ok(settings::validate(loaded))) {
        settings_ = loaded;
    }
}

Status App::setup() {
    if (started_) return Status::Ok;
    load_settings();
    own_.acft_cat = settings_.aircraft_type;

    Status s = p_.radio.begin();
    if (s != Status::Ok) return s;
    s = p_.radio.configure_mband(drivers::MbandConfig{});
    if (s != Status::Ok) return s;
    s = p_.radio.start_receive();
    if (s != Status::Ok) return s;

    started_ = true;
    return Status::Ok;
}

// Copy the pushed GNSS fix into the own-ship state and derive vertical speed
// from the altitude trend. own_ is what the protocol encoder, the alarm logic
// and every screen read — so this is the single point where "sensor" becomes
// "state".
void App::apply_gnss(uint32_t now_ms) {
    if (!have_pending_fix_) return;
    have_pending_fix_ = false;
    const gnss::GnssFix& f = pending_fix_;
    gnss_fixes_++;

    own_.fix_valid = f.valid;
    own_.utc_valid = f.utc_valid;
    own_.lat_1e7 = f.lat_1e7;
    own_.lon_1e7 = f.lon_1e7;
    own_.alt_m = f.alt_m;
    own_.speed_q = f.speed_q;
    own_.track_c9 = f.track_c9;
    own_.utc = f.utc;
    own_.sats = f.sats;
    own_.acft_cat = settings_.aircraft_type;

    clock_state_.utc_valid = f.utc_valid;

    // A barometer, once it has spoken, owns vertical speed. Keep the GNSS
    // reference moving anyway so losing the sensor falls back seamlessly.
    int16_t e8 = 0;
    const bool have =
        vs_from_alt_cm(f.alt_m * 100, now_ms, kVsWindowMs, vs_ref_alt_cm_, vs_ref_ms_, e8);
    if (have && !baro_active()) own_.climb_e8 = e8;
}

void App::on_baro(uint32_t pressure_pa, uint32_t now_ms) {
    const int32_t alt_cm = flight::pressure_to_alt_cm(pressure_pa);
    int16_t e8 = 0;
    if (vs_from_alt_cm(alt_cm, now_ms, kBaroVsWindowMs, baro_ref_alt_cm_, baro_ref_ms_, e8))
        own_.climb_e8 = e8;
}

bool App::vs_from_alt_cm(int32_t alt_cm, uint32_t now_ms, uint32_t window_ms, int32_t& ref_alt_cm,
                         uint32_t& ref_ms, int16_t& out_e8) const {
    if (ref_ms == 0) {  // first sample only anchors the window
        ref_ms = now_ms == 0 ? 1 : now_ms;
        ref_alt_cm = alt_cm;
        return false;
    }
    if (now_ms - ref_ms < window_ms) return false;

    const bool ok = flight::climb_e8_from_alt(alt_cm, ref_alt_cm, now_ms - ref_ms, out_e8);
    ref_ms = now_ms;
    ref_alt_cm = alt_cm;
    return ok;
}

// Pull received frames off the radio and turn valid ADS-L packets into traffic.
// CRC-failed frames are counted and dropped (§8.5 fail closed) — never shown.
void App::drain_radio(uint32_t now_ms) {
    drivers::RadioEvent ev = p_.radio.poll(rx_buf_, sizeof(rx_buf_));
    if (ev.type != drivers::RadioEventType::RxDone) {
        if (ev.type == drivers::RadioEventType::CrcError) rx_bad_++;
        return;
    }
    if (ev.len < protocol::AdslPacket::kTxBytes) {
        rx_bad_++;
        return;
    }

    protocol::AdslPacket p{};
    __builtin_memcpy(&p, rx_buf_, protocol::AdslPacket::kTxBytes);

    if (p.check_crc() != 0) {
        uint8_t err[protocol::AdslPacket::kDataBytes] = {0};
        if (p.correct(err) < 0 || p.check_crc() != 0) {
            rx_bad_++;
            return;
        }
    }
    p.descramble();

    messages::AircraftObs obs{};
    protocol::to_obs(p, traffic_now(now_ms), static_cast<uint16_t>(now_ms % 1000), ev.rssi_dbm,
                     messages::Source::AdslDirect, obs);
    table_.update(obs, traffic_now(now_ms));
    rx_ok_++;
}

// Assess every target against own-ship and drive the annunciator from the worst
// level. Alarm escalation logic itself lives in core/traffic/alarm.
void App::update_alarms() {
    uint8_t worst = 0;
    if (own_.fix_valid) {
        for (int i = 0; i < traffic::TrafficTable::kCapacity; i++) {
            traffic::Target* t = table_.at(i);
            if (!t || !t->used) continue;
            traffic::AlarmAssessment a = traffic::assess(own_, t->obs);
            t->alarm_level = a.level;
            if (a.level > worst) worst = a.level;
        }
    }
    if (worst != max_alarm_) {
        max_alarm_ = worst;
        dirty_ = true;
        if (p_.annunciator && settings_.alarm_enabled) {
            if (worst > 0)
                p_.annunciator->alarm(worst, settings_.alarm_volume);
            else
                p_.annunciator->silence();
        }
    }
}

void App::step(uint32_t now_ms) {
    const uint32_t dt = now_ms - last_ms_;
    last_ms_ = now_ms;

    config_.tick(now_ms);
    confirm_image_once_healthy();

    // 1) GNSS: fold the fix the shell pushed into own-ship state.
    apply_gnss(now_ms);

    // 2) radio watchdog: reinit if Rx has gone silent too long (§8 recovery).
    p_.radio.service(dt, product::kRadioNoRxReinitMs);

    // 3) drain the radio: decode ADS-L → traffic fusion table.
    drain_radio(now_ms);

    // 4) PPS-anchored slot plan for this phase of the 1 s TDMA frame.
    clock_state_.ms_since_pps += dt;
    plan_ = scheduler_.plan(static_cast<int>(now_ms % 1000), clock_state_);

    // 5) age stale traffic out, then re-assess collision alarms.
    table_.age_out(traffic_now(now_ms));
    update_alarms();

    // 6) refresh the e-paper: on the slow cadence, or immediately when dirty
    //    (e.g. a page change from a button press).
    if (p_.display && (dirty_ || now_ms - last_render_ms_ >= kRenderPeriodMs)) {
        last_render_ms_ = now_ms;
        render();
    }
}

// A fresh image swapped in by MCUboot is on probation: unless it declares
// itself good, the bootloader restores the previous one on the next boot. That
// guarantee is only worth something if "good" means more than "main() ran", so
// wait until the radio is up AND a GNSS fix has arrived — between them that
// exercises SPI, the SX1262, the UART and core/gnss. A build that boots but
// cannot talk to its own peripherals will be rolled back.
void App::confirm_image_once_healthy() {
    if (image_confirmed_ || p_.dfu == nullptr) return;
    if (!started_ || gnss_fixes_ == 0) return;
    p_.dfu->confirm();
    image_confirmed_ = true;
}

void App::on_button() {
    // Cycle to the next page enabled in settings.page_mask.
    const int n = static_cast<int>(Page::kCount);
    for (int i = 1; i <= n; i++) {
        int cand = (static_cast<int>(page_) + i) % n;
        if (settings_.page_mask & (1u << cand)) {
            page_ = static_cast<Page>(cand);
            break;
        }
    }
    dirty_ = true;  // force a full refresh of the new page next step
}

void App::set_backlight(bool on) {
    backlight_ = on;
    if (p_.display) p_.display->set_backlight(on);
}

void App::render() {
    const bool full = dirty_;
    dirty_ = false;
    fb_.clear(/*white=*/true);

    switch (page_) {
        case Page::Radar: {
            ui::RadarSnapshot snap;
            snap.have_fix = own_.fix_valid;
            snap.range_m = range_m_;
            snap.max_alarm = max_alarm_;
            snap.coverage = clock_state_.utc_valid;
            int n = 0;
            if (own_.fix_valid) {
                for (int i = 0; i < traffic::TrafficTable::kCapacity && n < kMaxRadarTargets; i++) {
                    const traffic::Target* t = table_.at(i);
                    if (!t || !t->used) continue;
                    int32_t north = 0, east = 0, up = 0;
                    if (!protocol::relative_ned(own_, t->obs, north, east, up)) continue;
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
            snap.have_data = own_.fix_valid;
            snap.imperial = settings_.units == settings::Units::Imperial;
            snap.alt_ft = to_feet(Metres(own_.alt_m)).v;
            // climb_e8 (eighth-m/s) → feet per minute: m/s * 196.85.
            snap.vs_fpm = (static_cast<int32_t>(own_.climb_e8) * 19685) / (8 * 100);
            ui::draw_altvs(fb_, snap);
            break;
        }
        case Page::Status:
        default: {
            ui::StatusSnapshot snap;
            snap.device_addr = settings_.device_addr;
            snap.fix_valid = own_.fix_valid;
            snap.utc_valid = own_.utc_valid;
            snap.pps_locked = clock_state_.pps_locked;
            snap.sats = own_.sats;
            snap.lat_1e7 = own_.lat_1e7;
            snap.lon_1e7 = own_.lon_1e7;
            snap.alt_m = own_.alt_m;
            snap.speed_q = own_.speed_q;
            snap.track_c9 = own_.track_c9;
            snap.climb_e8 = own_.climb_e8;
            snap.utc = own_.utc;
            snap.n_targets = table_.count();
            snap.imperial = settings_.units == settings::Units::Imperial;
            ui::draw_status(fb_, snap);
            break;
        }
    }

    p_.display->present(fb_, {0, 0, ui::Framebuffer::kW, ui::Framebuffer::kH},
                        full ? hal::Refresh::Full : hal::Refresh::Partial);
}

}  // namespace skyblip::go
