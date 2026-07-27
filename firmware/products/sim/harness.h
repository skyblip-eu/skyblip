// products/sim/harness.h — SimHarness: the "virtual T-Echo".
//
// This is the shared, frontend-agnostic core of the device simulator. It wires
// the real `App` to VIRTUAL peripherals and exposes a control surface. Both
// simulator frontends drive it identically:
//
//   products/sim/term_main.cpp   terminal frontend  (ASCII e-paper, TTY keys)
//   products/sim/web_main.cpp    browser frontend   (WASM, <canvas> + buttons)
//
// It runs the REAL firmware (core/ + ui/ + drivers + App) — no reimplementation.
// The simulated sensors are honest: GNSS produces real NMEA that the production
// parser decodes, and virtual aircraft are encoded as real ADS-L frames
// (scrambled + CRC) that the production receive path decodes. So exercising the
// simulator exercises the shipping logic, not a mock of it.
#ifndef SKYBLIP_PRODUCTS_SIM_HARNESS_H
#define SKYBLIP_PRODUCTS_SIM_HARNESS_H

#include <cmath>
#include <cstring>

#include "core/protocol/adsl.h"
#include "devices/drivers/sx1262.h"
#include "devices/models/annunciator.h"
#include "devices/models/clock.h"
#include "devices/models/display.h"
#include "devices/models/kvstore.h"
#include "devices/models/l76k.h"
#include "devices/models/link.h"
#include "devices/models/sx1262.h"
#include "products/skyblip/app.h"

namespace skyblip::sim {

// A virtual aircraft in the sky around own-ship, held in local N/E/U metres and
// re-broadcast ~1 Hz like a real transmitter would.
struct SimAircraft {
    bool used{false};
    uint32_t addr{0};
    double north_m{0}, east_m{0}, up_m{0};
    double speed_mps{30};
    double track_deg{270};
    int32_t climb_e8{0};
};

class SimHarness {
   public:
    static constexpr int kMaxAircraft = 8;

    SimHarness()
        : radio_(bus_, bus_, bus_.busy_pin, bus_.reset_pin, bus_.dio1_pin), app_(ports()) {}

    Status setup() { return app_.setup(); }

    void step(uint32_t now_ms) {
        clock_.set_millis(now_ms);
        gnss_.tick(now_ms);        // simulated GNSS → NMEA bytes
        service_aircraft(now_ms);  // virtual aircraft → real ADS-L frames
        app_.step(now_ms);
    }

    // ---- device controls ----------------------------------------------------
    void button() { app_.on_button(); }
    void backlight(bool on) { app_.set_backlight(on); }
    void power(bool on) {
        if (on)
            display_.power_on();
        else
            display_.power_off();
    }
    void set_range_m(int32_t m) { app_.set_range_m(m); }

    // ---- simulated sensors -------------------------------------------------
    models::L76k& gnss() { return gnss_; }
    void set_fix(bool on) { gnss_.fix = on; }
    void set_sats(int n) { gnss_.sats = static_cast<uint8_t>(n < 0 ? 0 : (n > 32 ? 32 : n)); }
    void set_altitude_m(int32_t m) { gnss_.alt_m = m; }
    void set_speed_kt(int32_t kt) { gnss_.speed_kt = kt; }
    void set_track_deg(int32_t deg) { gnss_.track_deg = ((deg % 360) + 360) % 360; }
    void set_climb_e1(int32_t e1) { gnss_.climb_mps_e1 = e1; }

    // ---- virtual traffic ---------------------------------------------------
    int add_aircraft(double north_m, double east_m, double up_m, double speed_mps = 30,
                     double track_deg = 270) {
        for (int i = 0; i < kMaxAircraft; i++) {
            if (acft_[i].used) continue;
            acft_[i] = SimAircraft{};
            acft_[i].used = true;
            acft_[i].addr = 0x300000u + static_cast<uint32_t>(i) + 1u;
            acft_[i].north_m = north_m;
            acft_[i].east_m = east_m;
            acft_[i].up_m = up_m;
            acft_[i].speed_mps = speed_mps;
            acft_[i].track_deg = track_deg;
            return i;
        }
        return -1;
    }
    // Convenience: a converging aircraft close enough to raise an alarm.
    int add_threat() { return add_aircraft(600, 200, 30, 40, 200); }
    void clear_aircraft() {
        for (auto& a : acft_) a.used = false;
        app_.traffic().clear();
    }
    int aircraft_count() const {
        int n = 0;
        for (const auto& a : acft_)
            if (a.used) n++;
        return n;
    }

    // ---- read side ---------------------------------------------------------
    const ui::Framebuffer& framebuffer() const { return display_.framebuffer(); }
    bool backlight_on() const { return display_.backlight(); }
    bool powered() const { return display_.powered(); }
    product::Page page() const { return app_.page(); }
    int present_count() const { return display_.present_count; }
    int traffic_count() const { return app_.traffic_count(); }
    uint8_t alarm_level() const { return ann_.level(); }
    uint32_t rx_ok() const { return app_.rx_ok(); }
    uint32_t rx_bad() const { return app_.rx_bad(); }
    const messages::OwnState& own() const { return app_.own(); }
    product::App& app() { return app_; }

   private:
    product::Ports ports() {
        product::Ports p{clock_, link_, radio_};
        p.display = &display_;
        p.kv = &kv_;
        p.annunciator = &ann_;
        p.gnss = &gnss_;
        p.device_addr = 0x0ABBCC;
        return p;
    }

    // Advance each virtual aircraft and inject ONE real ADS-L frame per call
    // (round-robin) — the radio model holds a single frame at a time, and App
    // drains one per step, so this mimics ~1 Hz per aircraft.
    void service_aircraft(uint32_t now_ms) {
        if (now_ms - last_acft_ms_ < 100) return;
        double dt = (now_ms - last_acft_ms_) / 1000.0;
        last_acft_ms_ = now_ms;

        for (auto& a : acft_) {
            if (!a.used) continue;
            const double rad = a.track_deg * 3.14159265358979 / 180.0;
            a.north_m += a.speed_mps * std::cos(rad) * dt;
            a.east_m += a.speed_mps * std::sin(rad) * dt;
        }
        for (int k = 0; k < kMaxAircraft; k++) {
            rr_ = (rr_ + 1) % kMaxAircraft;
            if (acft_[rr_].used) {
                transmit(acft_[rr_]);
                return;
            }
        }
    }

    // Encode a virtual aircraft as a genuine on-air ADS-L frame and hand it to
    // the radio model, so App's receive path (CRC → descramble → to_obs) runs.
    void transmit(const SimAircraft& a) {
        const messages::OwnState& own = app_.own();
        const double coslat = std::cos(own.lat_1e7 / 1e7 * 3.14159265358979 / 180.0);
        int32_t lat = own.lat_1e7 + static_cast<int32_t>(a.north_m * 1e7 / 111320.0);
        int32_t lon = own.lon_1e7;
        if (coslat > 0.01) lon += static_cast<int32_t>(a.east_m * 1e7 / (111320.0 * coslat));

        protocol::AdslPacket p;
        p.init();
        p.set_address(a.addr);
        p.set_addr_table(6);
        p.TimeStamp = static_cast<uint8_t>((last_acft_ms_ / 250) & 0x3F);
        p.FlightState = 2;  // airborne
        p.AcftCat = 4;      // glider
        p.Emergency = 1;
        p.set_lat_1e7(lat);
        p.set_lon_1e7(lon);
        p.set_alt_m(own.alt_m + static_cast<int32_t>(a.up_m));
        p.set_speed_q(static_cast<uint16_t>(a.speed_mps * 4));
        p.set_climb_e8(static_cast<int16_t>(a.climb_e8));
        p.set_track_c9(static_cast<uint16_t>(a.track_deg * 512 / 360) & 0x1FF);
        p.SourceIntegrity = 3;
        p.DesignAssurance = 2;
        p.NavigIntegrity = 11;
        p.HorizAccuracy = 6;
        p.VertAccuracy = 3;
        p.VelAccuracy = 2;
        p.scramble();
        p.set_crc();

        uint8_t wire[protocol::AdslPacket::kTxBytes];
        std::memcpy(wire, &p, sizeof(wire));
        bus_.queue_rx(wire, static_cast<uint8_t>(sizeof(wire)));
    }

    models::Clock clock_;
    models::Link link_;
    models::Sx1262 bus_;
    models::KvStore kv_;
    models::Display display_;
    models::Annunciator ann_;
    models::L76k gnss_;
    drivers::Sx1262 radio_;  // declared after bus_ (ctor uses it)
    product::App app_;       // declared last (ctor uses all of the above)

    SimAircraft acft_[kMaxAircraft]{};
    uint32_t last_acft_ms_{0};
    int rr_{0};
};

}  // namespace skyblip::sim

#endif
