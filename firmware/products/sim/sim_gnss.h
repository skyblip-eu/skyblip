// products/sim/sim_gnss.h — a simulated L76K GNSS receiver.
//
// It is an io::Uart, so App reads it exactly as it reads the real receiver: this
// generates genuine NMEA-0183 $GPRMC/$GPGGA sentences (built with the production
// formatters + nmea_finish checksum) and the REAL core/gnss parser decodes them.
// Nothing is stubbed at the state level — moving the aircraft here exercises the
// whole chain: NMEA → parser → own-ship state → screens/alarms/protocol.
#ifndef SKYBLIP_PRODUCTS_SIM_SIM_GNSS_H
#define SKYBLIP_PRODUCTS_SIM_SIM_GNSS_H

#include <cmath>
#include <string>

#include "core/protocol/nmea_out.h"
#include "core/util/format.h"
#include "devices/io/io.h"

namespace skyblip::sim {

class SimGnss : public io::Uart {
   public:
    // ---- simulated sensor state (driven by the frontends) -------------------
    bool fix{true};
    uint8_t sats{9};
    int32_t lat_1e7{485000000};  // 48.5 N
    int32_t lon_1e7{85000000};   //  8.5 E
    int32_t alt_m{1000};
    int32_t speed_kt{45};
    int32_t track_deg{90};
    int32_t climb_mps_e1{0};  // tenths of m/s, applied to alt over time
    uint32_t utc_sod{12 * 3600 + 34 * 60 + 56};

    // ---- io::Uart ----------------------------------------------------------
    size_t write(const uint8_t*, size_t len) override { return len; }  // no uplink
    size_t read(uint8_t* data, size_t cap) override {
        size_t n = pending_.size() < cap ? pending_.size() : cap;
        for (size_t i = 0; i < n; i++) data[i] = static_cast<uint8_t>(pending_[i]);
        pending_.erase(0, n);
        return n;
    }
    size_t available() override { return pending_.size(); }

    // Advance the simulated aircraft and emit a 1 Hz NMEA burst.
    void tick(uint32_t now_ms) {
        if (last_ms_ == 0) last_ms_ = now_ms;
        uint32_t dt = now_ms - last_ms_;
        if (dt < 1000) return;
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
        alt_m += static_cast<int32_t>(climb_mps_e1 * secs / 10.0);
        utc_sod = (utc_sod + static_cast<uint32_t>(secs + 0.5)) % 86400u;

        emit_rmc();
        emit_gga();
    }

   private:
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

    // $GPGGA,hhmmss,ddmm.mmmm,N,dddmm.mmmm,E,q,sats,hdop,alt,M,,,,*cs
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
        n += fmt_string(s + n, ",0.9,");
        n += fmt_int(s + n, alt_m, 1, 0, true);
        n += fmt_string(s + n, ",M,,,,");
        n = protocol::nmea_finish(s, n);
        pending_.append(s, static_cast<size_t>(n));
    }

    std::string pending_;
    uint32_t last_ms_{0};
};

}  // namespace skyblip::sim

#endif
