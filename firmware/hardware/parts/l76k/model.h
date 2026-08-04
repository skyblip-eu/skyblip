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
#include "hardware/parts/l76k/l76k.h"

namespace skyblip::models {

// The model is the receiver AND the wire to it: io::UartRate is the seam the
// platform's UART fills on silicon, and here it is the rate the MCU end is set
// to. Bytes crossing a rate mismatch are framing errors in both directions,
// which is what "the receiver came up at 38400" actually looks like.
class L76k : public io::Uart, public io::UartRate {
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
    // INFO: fc 03aug26 Degrees per second, positive to the right. The track this
    // model emits is the only thing own-ship can differentiate a turn rate out
    // of, so a thermalling own-ship is expressed here and nowhere else.
    double turn_dps{0};
    int32_t climb_mps_e1{0};  // tenths of m/s, applied to alt over time
    uint32_t utc_sod{12 * 3600 + 34 * 60 + 56};
    // RMC field 9, ddmmyy. "010180" is the MTK fake date a receiver with no
    // almanac reports beside a position that looks perfectly ordinary.
    const char* date{"010125"};

    // INFO: gn 09Jun25 The AT6558 core in the L76K takes $PCAS configuration
    // sentences and never acknowledges them: the only evidence they landed is
    // the receiver's own behaviour changing (oss/SoftRF-lyusupov
    // .../src/driver/GNSS.cpp:1029-1057). This model behaves the same way.
    static constexpr uint32_t kFactoryPeriodMs = 1000;
    static constexpr uint8_t kAviationDynamicModel = 6;

    // A receiver that hears every command and obeys none: a dead TX line, a
    // clone with a different command set, a module held in a firmware-update mode.
    bool accepts_commands{true};

    // INFO: fc 03aug26 The L76K wakes on UART activity and is deaf for about
    // half a second afterwards, which is why SoftRF spends a byte and 500 ms
    // before it says anything that matters (oss/SoftRF-lyusupov
    // .../src/driver/GNSS.cpp:1383-1387). Default false: a receiver already
    // running is the ordinary case, and `asleep = true` is the cold one.
    static constexpr uint32_t kWakeMs = 500;
    bool asleep{false};

    // The rate the receiver itself is talking at. The MCU end is set through
    // io::UartRate, and the two only agree because someone made them.
    uint32_t baud{parts::L76k::kBaudRate};

    // INFO: fc 03aug26 $PCAS06 is answered with the CASIC firmware banner. A
    // clone that takes $PCAS sentences but never introduces itself is exactly
    // what the handshake exists to expose.
    bool answers_identification{true};
    const char* firmware_version{"URANUS5,V5.1.0.0"};

    // A cold start throws the orbit data away, and the receiver is blind until
    // it has decoded a fresh almanac. This is the twenty minutes a pilot reads
    // as a broken device, compressed to the datasheet's cold TTFF.
    static constexpr uint32_t kColdStartTtffMs = 30000;
    static constexpr uint32_t kRebootMs = 300;

    uint32_t solution_period_ms{kFactoryPeriodMs};
    uint8_t constellations{0};
    uint8_t dynamic_model{0};
    bool sentence_set_applied{false};
    bool gga_enabled{false};
    bool gsa_enabled{false};
    bool rmc_enabled{false};
    uint32_t commands_seen{0};
    uint32_t commands_rejected{0};
    uint32_t wake_bytes{0};
    uint32_t deaf_bytes{0};
    uint32_t garbled_bytes{0};
    uint32_t baud_changes{0};
    uint32_t restarts{0};
    uint32_t factory_resets{0};
    // Which $PCAS10 argument arrived last, so a test can tell a hot restart from
    // the cold start that is the whole point of asking.
    int last_restart_kind{-1};

    bool aviation_dynamic_model() const { return dynamic_model == kAviationDynamicModel; }

    // Everything the receiver heard, in the order it heard it: "WAKE,PCAS06,...".
    // A driver that sends the right sentences in the wrong order configures a
    // receiver that was not listening yet.
    const std::string& heard() const { return heard_; }

    // io::UartRate: the MCU end of the link retunes. Always possible here;
    // whether it is possible on silicon is the platform's answer, not the chip's.
    bool set(uint32_t rate) override {
        port_baud_ = rate;
        baud_changes++;
        return true;
    }
    uint32_t port_baud() const { return port_baud_; }

    size_t write(const uint8_t* data, size_t len) override {
        for (size_t i = 0; i < len; i++) {
            // A rate mismatch is not silence: the bits arrive and frame as
            // rubbish, which fails the checksum and is therefore indistinguishable
            // from nothing having been said.
            if (port_baud_ != baud) {
                garbled_bytes++;
                continue;
            }
            if (rebooting()) continue;
            if (asleep) {
                asleep = false;
                deaf_since_ms_ = last_tick_ms_;
                deaf_ = true;
                wake_bytes++;
                heard_ += "WAKE,";
                continue;  // the byte that wakes it is the byte it loses
            }
            if (deaf()) {
                deaf_bytes++;
                command_len_ = 0;
                continue;
            }
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
        // It is talking and we cannot frame a byte of it: the sentences went
        // past on the wire and are gone, they are not queued up waiting for
        // someone to fix the rate.
        if (port_baud_ != baud) {
            pending_.clear();
            return 0;
        }
        size_t n = pending_.size() < cap ? pending_.size() : cap;
        for (size_t i = 0; i < n; i++) data[i] = static_cast<uint8_t>(pending_[i]);
        pending_.erase(0, n);
        return n;
    }
    size_t available() override { return pending_.size(); }

    // Advance own-ship along its track and emit an NMEA burst per solution.
    void tick(uint32_t now_ms) {
        last_tick_ms_ = now_ms;
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
        // A receiver nobody has spoken to yet is not producing, and one that is
        // rebooting after $PCAS10 has nothing to say either.
        if (asleep || rebooting()) {
            last_ms_ = now_ms;
            return;
        }
        last_ms_ = now_ms;

        // Integrate position along the current track at the current speed. In a
        // turn that is the track halfway through the step, or the path is a
        // polygon drawn outside the circle actually flown.
        const double v_mps = speed_kt * 0.514444;
        const double secs = dt / 1000.0;
        const double rad = (track_deg + turn_dps * secs * 0.5) * 3.14159265358979 / 180.0;
        const double north_m = v_mps * std::cos(rad) * secs;
        const double east_m = v_mps * std::sin(rad) * secs;
        lat_1e7 += static_cast<int32_t>(north_m * 1e7 / 111320.0);
        const double coslat = std::cos(lat_1e7 / 1e7 * 3.14159265358979 / 180.0);
        if (coslat > 0.01) lat_lon_advance(east_m, coslat);
        turned_deg_ += turn_dps * secs;
        const int32_t whole_deg = static_cast<int32_t>(turned_deg_);
        turned_deg_ -= whole_deg;
        track_deg = ((track_deg + whole_deg) % 360 + 360) % 360;
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
    double turned_deg_{0};
    char command_[kCommandCap]{};
    int command_len_{0};
    // Three windows, each a START instant plus a flag rather than an end instant.
    // core/../hal/clock.h states the rule and section M found the bug it prevents:
    // an end instant compared with `<` inverts across the 49.7-day wrap, and here
    // it would invert PERMISSIVELY - a window that ends immediately is a model
    // that answers when the real part would not, which is the one direction a
    // test double must never fail in.
    uint32_t last_tick_ms_{0};
    uint32_t deaf_since_ms_{0};
    uint32_t reboot_since_ms_{0};
    uint32_t cold_since_ms_{0};
    bool deaf_{false};
    bool rebooting_{false};
    bool cold_{false};
    uint32_t port_baud_{parts::L76k::kBaudRate};
    std::string heard_;

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
        for (int i = 1; i < command_len_ && i < 7; i++) heard_ += command_[i];
        heard_ += ',';

        // The version banner is the part naming itself, not a setting: a
        // receiver that obeys nothing still answers it.
        if (starts_with(command_, command_len_, "$PCAS06,")) {
            if (answers_identification) emit_version();
            return;
        }
        if (!accepts_commands) return;
        if (starts_with(command_, command_len_, "$PCAS04,"))
            constellations = static_cast<uint8_t>(argument(command_, command_len_, 8));
        else if (starts_with(command_, command_len_, "$PCAS03,"))
            apply_sentence_set();
        else if (starts_with(command_, command_len_, "$PCAS11,"))
            dynamic_model = static_cast<uint8_t>(argument(command_, command_len_, 8));
        else if (starts_with(command_, command_len_, "$PCAS02,"))
            solution_period_ms = argument(command_, command_len_, 8);
        else if (starts_with(command_, command_len_, "$PCAS10,"))
            apply_restart(static_cast<int>(argument(command_, command_len_, 8)));
    }

    // $PCAS03,GGA,GLL,GSA,GSV,RMC,VTG,... each 0 or 1. Which sentences we asked
    // for is not a bool: GSA is the one that carries VDOP, and its absence is a
    // decision core/protocol/adsl.cpp has to live with.
    void apply_sentence_set() {
        int field = 0;
        int at = 8;
        while (at < command_len_ && field < 5) {
            const bool on = command_[at] == '1';
            if (field == 0) gga_enabled = on;
            if (field == 2) gsa_enabled = on;
            if (field == 4) rmc_enabled = on;
            while (at < command_len_ && command_[at] != ',') at++;
            at++;
            field++;
        }
        sentence_set_applied = true;
    }

    // $PCAS10,n: 0 hot, 1 warm, 2 cold, 3 factory. The module reboots either
    // way; a cold start also throws the orbit data away, and a factory reset
    // takes every setting we applied with it.
    void apply_restart(int kind) {
        restarts++;
        last_restart_kind = kind;
        reboot_since_ms_ = last_tick_ms_;
        rebooting_ = true;
        pending_.clear();
        if (kind >= 2) {
            cold_since_ms_ = last_tick_ms_;
            cold_ = true;
        }
        if (kind < 3) return;
        factory_resets++;
        constellations = 0;
        dynamic_model = 0;
        sentence_set_applied = false;
        gga_enabled = gsa_enabled = rmc_enabled = false;
        solution_period_ms = kFactoryPeriodMs;
    }

    bool deaf() const { return deaf_ && last_tick_ms_ - deaf_since_ms_ < kWakeMs; }
    bool rebooting() const { return rebooting_ && last_tick_ms_ - reboot_since_ms_ < kRebootMs; }
    bool cold() const { return cold_ && last_tick_ms_ - cold_since_ms_ < kColdStartTtffMs; }
    bool solving() const { return fix && !cold(); }

    // $GPTXT,01,01,02,SW=<version>: the reply SoftRF matches on
    // (oss/SoftRF-lyusupov .../src/driver/GNSS.cpp:981-1010, 1015-1025).
    void emit_version() {
        char s[128];
        int n = fmt_string(s, "$GPTXT,01,01,02,SW=");
        n += fmt_string(s + n, firmware_version);
        n = protocol::nmea_finish(s, n);
        pending_.append(s, static_cast<size_t>(n));
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
        n += fmt_string(s + n, solving() ? ",A," : ",V,");
        n += fmt_nmea_lat(s + n, lat_1e7);
        s[n++] = ',';
        n += fmt_nmea_lon(s + n, lon_1e7);
        s[n++] = ',';
        n += fmt_uint(s + n, static_cast<uint32_t>(speed_kt < 0 ? 0 : speed_kt), 1);
        n += fmt_string(s + n, ".0,");
        n += fmt_uint(s + n, static_cast<uint32_t>((track_deg % 360 + 360) % 360), 1);
        s[n++] = ',';
        n += fmt_string(s + n, date);
        n += fmt_string(s + n, ",,,A");
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
        n += fmt_uint(s + n, solving() ? 1u : 0u, 1);
        s[n++] = ',';
        n += fmt_uint(s + n, solving() ? sats : uint8_t{0}, 2);
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
