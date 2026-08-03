#include "core/flight/state.h"

namespace skyblip::flight {

namespace {
constexpr int32_t kE8PerSpeedQ = 2;
}

int32_t motion_e8(const FlightSample& sample) {
    const int32_t climb = sample.climb_valid ? sample.climb_e8 : 0;
    int32_t motion = static_cast<int32_t>(sample.speed_q) * kE8PerSpeedQ +
                     kClimbWeight * (climb < 0 ? -climb : climb);
    if (sample.hdop_e2 > kDopUnityE2)
        motion = motion * kDopUnityE2 / static_cast<int32_t>(sample.hdop_e2);
    return motion;
}

bool FlightMonitor::jerky(uint16_t previous_q, uint16_t now_q) {
    const int32_t previous = previous_q;
    const int32_t now = now_q;
    return now > previous * kJerkSpeedRatio || previous > now * kJerkSpeedRatio;
}

FlightState FlightMonitor::update(const FlightSample& sample) {
    if (!sample.fix_valid) {
        armed_ = false;
        return FlightState::Unknown;
    }

    const uint32_t dt_ms = armed_ ? sample.at_ms - last_ms_ : 0;
    const uint16_t previous_speed_q = last_speed_q_;
    armed_ = true;
    last_ms_ = sample.at_ms;
    last_speed_q_ = sample.speed_q;

    const bool too_high_to_be_down = sample.alt_msl_m > kAlwaysFlyingAltM;
    const int32_t motion = motion_e8(sample);

    // The hold protects a TRANSITION. At the first solution there is nothing to
    // transition from, and answering "unknown" for five seconds hands the
    // transmit rate and the update lockout their wrong default in exactly the
    // case that matters: a device switched on, or rebooted, already in the air.
    if (state_ == FlightState::Unknown) {
        state_ = too_high_to_be_down || motion >= kTakeoffMotionE8 ? FlightState::Airborne
                                                                   : FlightState::OnGround;
        hold_ms_ = 0;
        return state_;
    }
    if (dt_ms == 0 || dt_ms > kMaxSampleGapMs) return state_;

    if (state_ == FlightState::Airborne) {
        if (too_high_to_be_down || motion >= kLandingMotionE8) {
            const uint32_t repaid = dt_ms / kLandingRecoveryDivisor;
            hold_ms_ = hold_ms_ > repaid ? hold_ms_ - repaid : 0;
            return state_;
        }
        hold_ms_ += dt_ms;
        if (hold_ms_ >= kLandingHoldMs) {
            state_ = FlightState::OnGround;
            hold_ms_ = 0;
        }
        return state_;
    }

    if (!too_high_to_be_down && motion < kTakeoffMotionE8) {
        hold_ms_ = 0;
        state_ = FlightState::OnGround;
        return state_;
    }
    if (jerky(previous_speed_q, sample.speed_q)) return state_;

    hold_ms_ += dt_ms;
    if (hold_ms_ >= kTakeoffHoldMs) {
        state_ = FlightState::Airborne;
        hold_ms_ = 0;
    }
    return state_;
}

}  // namespace skyblip::flight
