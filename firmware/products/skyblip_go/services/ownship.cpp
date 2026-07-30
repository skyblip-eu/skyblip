#include "products/skyblip_go/services/ownship.h"

#include "core/flight/atmosphere.h"

namespace skyblip::go {

// ADS-L G.1.4 FlightState: 0 unknown, 1 on ground, 2 airborne. The gap between
// the two thresholds is what stops a taxiing aircraft toggling its transmit
// rate between 1 Hz and 0.1 Hz every fix.
uint8_t OwnshipService::flight_state_from(uint16_t speed_q, uint8_t current, bool fix_valid) {
    if (!fix_valid) return 0;
    if (speed_q >= kAirborneSpeedQ) return 2;
    if (speed_q < kGroundSpeedQ) return 1;
    return current == 0 ? 1 : current;
}

void OwnshipService::tick(uint32_t now_ms) {
    gnss::GnssFix fix{};
    while (context_.bus.gnss.pop(fix)) apply_fix(fix, now_ms);

    messages::BaroSample sample{};
    while (context_.bus.baro.pop(sample)) apply_baro(sample);

    context_.state.baro_active = baro_active();
}

void OwnshipService::apply_fix(const gnss::GnssFix& f, uint32_t now_ms) {
    messages::OwnState& own = context_.state.own;
    context_.state.gnss_fixes++;

    own.fix_valid = f.valid;
    own.utc_valid = f.utc_valid;
    own.lat_1e7 = f.lat_1e7;
    own.lon_1e7 = f.lon_1e7;
    own.alt_m = f.alt_m;
    own.speed_q = f.speed_q;
    own.track_c9 = f.track_c9;
    own.utc = f.utc;
    own.fix_ms = now_ms;
    own.sats = f.sats;
    own.aircraft_cat = context_.state.settings.aircraft_type;
    own.flight_state = flight_state_from(own.speed_q, own.flight_state, f.valid);

    context_.state.clock.utc_valid = f.utc_valid;

    // A barometer, once it has spoken, owns vertical speed. The GNSS reference
    // keeps moving anyway so losing the sensor falls back seamlessly.
    int16_t e8 = 0;
    const bool have =
        vs_from_alt_cm(f.alt_m * 100, now_ms, kVsWindowMs, vs_ref_alt_cm_, vs_ref_ms_, e8);
    if (have && !baro_active()) {
        own.climb_e8 = e8;
        own.climb_valid = true;
    }

    update_turn_rate(now_ms);
}

void OwnshipService::apply_baro(const messages::BaroSample& sample) {
    context_.state.pressure_pa = sample.pressure_pa;
    const int32_t alt_cm = flight::pressure_to_alt_cm(sample.pressure_pa);
    int16_t e8 = 0;
    if (vs_from_alt_cm(alt_cm, sample.at_ms, kBaroVsWindowMs, baro_ref_alt_cm_, baro_ref_ms_, e8)) {
        context_.state.own.climb_e8 = e8;
        context_.state.own.climb_valid = true;
    }
}

void OwnshipService::update_turn_rate(uint32_t now_ms) {
    const uint16_t track_c9 = context_.state.own.track_c9;
    if (turn_ref_ms_ == 0) {
        turn_ref_ms_ = now_ms == 0 ? 1 : now_ms;
        turn_ref_track_c9_ = track_c9;
        return;
    }
    const uint32_t dt = now_ms - turn_ref_ms_;
    if (dt < kTurnWindowMs) return;

    // cordic9: 512 units = 360 deg. Wrap to the short way round so 359 -> 001
    // reads as +2 deg, not -358.
    const int32_t diff = ((static_cast<int32_t>(track_c9) - turn_ref_track_c9_ + 768) % 512) - 256;
    context_.state.turn_dps =
        static_cast<int16_t>((diff * 45 * 1000) / (64 * static_cast<int32_t>(dt)));
    turn_ref_ms_ = now_ms;
    turn_ref_track_c9_ = track_c9;
}

bool OwnshipService::vs_from_alt_cm(int32_t alt_cm, uint32_t now_ms, uint32_t window_ms,
                                    int32_t& ref_alt_cm, uint32_t& ref_ms, int16_t& out_e8) const {
    if (ref_ms == 0) {
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

}  // namespace skyblip::go
