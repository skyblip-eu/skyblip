// hardware/parts/l76k/model.h: a model of the L76K GNSS receiver at the io::Uart
// seam, so the REAL parser runs against it unchanged: it emits genuine NMEA-0183
// $GPRMC/$GPGGA sentences (production formatters + nmea_finish checksum) and it
// consumes genuine $PCAS configuration sentences, checksum first.
// Nothing is stubbed at the state level: moving the aircraft here exercises the
// whole chain: NMEA -> parser -> own-ship state -> screens/alarms/protocol.
#ifndef SKYBLIP_HARDWARE_MODEL_L76K_H
#define SKYBLIP_HARDWARE_MODEL_L76K_H

#include <cmath>
#include <string>

#include "core/gnss/nmea.h"
#include "core/protocol/nmea_out.h"
#include "core/util/format.h"
#include "hardware/io/io.h"

namespace skyblip::models {

class L76k : public io::Uart {
   public:
    // Modelled receiver state, driven by the caller.
    bool fix{true};
    uint8_t sats{9};
    int32_t lat_1e7{485000000};  // 48.5 N
    int32_t lon_1e7{85000000};   //  8.5 E
    // Height above the WGS-84 ellipsoid. The GGA this emits carries field 9 as
    // alt_m - geoid_separation_m, which is what a receiver actually reports.
    int32_t alt_m{1000};
    int32_t geoid_separation_m{47};
    bool emit_geoid_separation{true};
    uint16_t hdop_e2{90};
    int32_t speed_kt{45};
    int32_t track_deg{90};
    int32_t climb_mps_e1{0};  // tenths of m/s, applied to alt over time
    uint32_t utc_sod{12 * 3600 + 34 * 60 + 56};

    // INFO: gn 09Jun25 The AT6558 core in the L76K takes $PCAS configuration
    // sentences and never acknowledges them: the only evidence they landed is
    // the receiver's own behaviour changing (oss/SoftRF-lyusupov
    // .../src/driver/GNSS.cpp:1029-1057). This model behaves the same way.
    static constexpr uint32_t kFactoryPeriodMs = 1000;
    static constexpr uint8_t kAviationDynamicModel = 6;

    // A receiver that hears every command and obeys none: a dead TX line, a
    // clone with a different command set, a module held in a firmware-update mode.
    bool accepts_commands{true};

    uint32_t solution_period_ms{kFactoryPeriodMs};
    uint8_t constellations{0};
    uint8_t dynamic_model{0};
    bool sentence_set_applied{false};
    uint32_t commands_seen{0};
    uint32_t commands_rejected{0};

    bool aviation_dynamic_model() const { return dynamic_model == kAviationDynamicModel; }

    size_t write(const uint8_t* data, size_t len) override {
        for (size_t i = 0; i < len; i++) {
            const char c = static_cast<char>(data[i]);
            if (c == '$') {
                command_len_ = 0;
                command_[command_len_++] = c;
            } else if (c == '\r' || c == '\n') {
                if (command_len_ > 0) apply_command();
                command_len_ = 0;
            } else if (command_len_ > 0 && command_len_ < kCommandCap - 1) {
                command_[command_len_++] = c;
            }
        }
        return len;
    }
    size_t read(uint8_t* data, size_t cap) override {
        size_t n = pending_.size() < cap ? pending_.size() : cap;
        for (size_t i = 0; i < n; i++) data[i] = static_cast<uint8_t>(pending_[i]);
        pending_.erase(0, n);
        return n;
    }
    size_t available() override { return pending_.size(); }

    // Advance own-ship along its track and emit an NMEA burst per solution.
    void tick(uint32_t now_ms) {
        // First tick only anchors the cadence. Anchoring on `last_ms_ == 0`
        // instead would re-anchor on every call made at t=0, which is a legal
        // timestamp, not an "unset" marker.
        if (!armed_) {
            armed_ = true;
            last_ms_ = now_ms;
            return;
        }
        uint32_t dt = now_ms - last_ms_;
        if (dt < solution_period_ms) return;
        last_ms_ = now_ms;

        // Integrate position along the current track at the current speed.
        const double v_mps = speed_kt * 0.514444;
        const double secs = dt / 1000.0;
        const double rad = track_deg * 3.14159265358979 / 180.0;
        const double north_m = v_mps * std::cos(rad) * secs;
        const double east_m = v_mps * std::sin(rad) * secs;
        lat_1e7 += static_cast<int32_t>(north_m * 1e7 / 111320.0);
        const double coslat = std::cos(lat_1e7 / 1e7 * 3.14159265358979 / 180.0);
        if (coslat > 0.01) lat_lon_advance(east_m, coslat);
        // Metres per solution, not per second: at 5 Hz a 3 m/s climb is 0.6 m a
        // step, and truncating that to an integer would report level flight.
        climbed_m_ += climb_mps_e1 * secs / 10.0;
        const int32_t whole_m = static_cast<int32_t>(climbed_m_);
        alt_m += whole_m;
        climbed_m_ -= whole_m;
        // NMEA carries whole seconds here, so the sub-second solutions of one
        // second all stamp the same UTC: their freshness is their arrival time.
        sod_ms_ += dt;
        utc_sod = (utc_sod + sod_ms_ / 1000u) % 86400u;
        sod_ms_ %= 1000u;

        emit_rmc();
        emit_gga();
    }

   private:
    static constexpr int kCommandCap = 64;

    uint32_t sod_ms_{0};
    double climbed_m_{0};
    char command_[kCommandCap]{};
    int command_len_{0};

    static bool starts_with(const char* s, int len, const char* prefix) {
        int i = 0;
        for (; prefix[i]; i++) {
            if (i >= len || s[i] != prefix[i]) return false;
        }
        return true;
    }
    static uint32_t argument(const char* s, int len, int at) {
        uint32_t v = 0;
        for (int i = at; i < len && s[i] >= '0' && s[i] <= '9'; i++) v = v * 10 + (s[i] - '0');
        return v;
    }

    void apply_command() {
        commands_seen++;
        if (!gnss::nmea_checksum_ok(command_, command_len_)) {
            commands_rejected++;
            return;
        }
        if (!accepts_commands) return;
        if (starts_with(command_, command_len_, "$PCAS04,"))
            constellations = static_cast<uint8_t>(argument(command_, command_len_, 8));
        else if (starts_with(command_, command_len_, "$PCAS03,"))
            sentence_set_applied = true;
        else if (starts_with(command_, command_len_, "$PCAS11,"))
            dynamic_model = static_cast<uint8_t>(argument(command_, command_len_, 8));
        else if (starts_with(command_, command_len_, "$PCAS02,"))
            solution_period_ms = argument(command_, command_len_, 8);
    }

    void lat_lon_advance(double east_m, double coslat) {
        lon_1e7 += static_cast<int32_t>(east_m * 1e7 / (111320.0 * coslat));
    }

    int put_time(char* s) {
        int n = 0;
        n += fmt_uint(s + n, utc_sod / 3600u, 2);
        n += fmt_uint(s + n, (utc_sod / 60u) % 60u, 2);
        n += fmt_uint(s + n, utc_sod % 60u, 2);
        return n;
    }

    // $GPRMC,hhmmss,A,ddmm.mmmm,N,dddmm.mmmm,E,speed_kn,track,ddmmyy,,,A*cs
    void emit_rmc() {
        char s[128];
        int n = fmt_string(s, "$GPRMC,");
        n += put_time(s + n);
        n += fmt_string(s + n, fix ? ",A," : ",V,");
        n += fmt_nmea_lat(s + n, lat_1e7);
        s[n++] = ',';
        n += fmt_nmea_lon(s + n, lon_1e7);
        s[n++] = ',';
        n += fmt_uint(s + n, static_cast<uint32_t>(speed_kt < 0 ? 0 : speed_kt), 1);
        n += fmt_string(s + n, ".0,");
        n += fmt_uint(s + n, static_cast<uint32_t>((track_deg % 360 + 360) % 360), 1);
        n += fmt_string(s + n, ",010125,,,A");
        n = protocol::nmea_finish(s, n);
        pending_.append(s, static_cast<size_t>(n));
    }

    // $GPGGA,hhmmss,ddmm.mmmm,N,dddmm.mmmm,E,q,sats,hdop,msl,M,separation,M,,*cs
    void emit_gga() {
        char s[128];
        int n = fmt_string(s, "$GPGGA,");
        n += put_time(s + n);
        s[n++] = ',';
        n += fmt_nmea_lat(s + n, lat_1e7);
        s[n++] = ',';
        n += fmt_nmea_lon(s + n, lon_1e7);
        s[n++] = ',';
        n += fmt_uint(s + n, fix ? 1u : 0u, 1);
        s[n++] = ',';
        n += fmt_uint(s + n, sats, 2);
        s[n++] = ',';
        n += fmt_uint(s + n, hdop_e2, 3, 2);
        s[n++] = ',';
        n += fmt_int(s + n, alt_m - geoid_separation_m, 1, 0, true);
        n += fmt_string(s + n, ",M,");
        if (emit_geoid_separation) n += fmt_int(s + n, geoid_separation_m, 1, 0, true);
        n += fmt_string(s + n, ",M,,");
        n = protocol::nmea_finish(s, n);
        pending_.append(s, static_cast<size_t>(n));
    }

    std::string pending_;
    uint32_t last_ms_{0};
    bool armed_{false};
};

}  // namespace skyblip::models

#endif
