#include "core/gnss/validity.h"

namespace skyblip::gnss {

namespace {
int32_t magnitude(int32_t v) { return v < 0 ? -v : v; }
}

void FixValidity::reset() {
    have_rmc_ = false;
    have_gga_ = false;
    rmc_solution_ = false;
    gga_solution_ = false;
    date_ok_ = false;
    jumped_ = false;
    have_previous_ = false;
}

void FixValidity::observe(const GnssFix& fix, Sentence which, uint32_t now_ms) {
    if (which == Sentence::Gga) {
        have_gga_ = true;
        gga_ms_ = now_ms;
        gga_solution_ = fix.fix_quality >= kQualityGps && fix.fix_quality <= kQualityFloatRtk &&
                        fix.alt_msl_valid;
        return;
    }
    if (which != Sentence::Rmc) return;

    have_rmc_ = true;
    rmc_ms_ = now_ms;
    rmc_solution_ = fix.valid;
    date_ok_ = fix.utc_valid;

    // The jump gate compares consecutive SOLUTIONS, so losing the fix drops the
    // reference: a receiver that reacquires somewhere else has not jumped, it
    // has been switched off in a car. moshe-braner keeps the stale reference and
    // eats one bad fix on reacquisition; we would rather not transmit one.
    if (!fix.valid) {
        have_previous_ = false;
        jumped_ = false;
        return;
    }

    jumped_ = have_previous_ &&
              (magnitude(fix.lat_1e7 - prev_lat_1e7_) > kMaxLatitudeJump1e7 ||
               magnitude(fix.lon_1e7 - prev_lon_1e7_) > kMaxLongitudeJump1e7);
    // The new position becomes the reference either way: one implausible step
    // costs one fix, not every fix after it. Two receivers disagreeing about
    // where we are is a stuck state; a single spike is a spike.
    prev_lat_1e7_ = fix.lat_1e7;
    prev_lon_1e7_ = fix.lon_1e7;
    have_previous_ = true;
}

FixReject FixValidity::check(uint32_t now_ms) {
    const FixReject reason = evaluate(now_ms);
    // A run of refusals for the same reason is one event, not one per poll: the
    // counter support reads should say "the antenna came off twice", not "the
    // antenna came off four thousand times".
    if (reason != FixReject::None && reason != last_reject_) rejected_++;
    last_reject_ = reason;
    return reason;
}

FixReject FixValidity::evaluate(uint32_t now_ms) const {
    FixReject reason = FixReject::None;
    if (!have_rmc_)
        reason = FixReject::MissingRmc;
    else if (!have_gga_)
        reason = FixReject::MissingGga;
    else if (now_ms - rmc_ms_ > kSentenceMaxAgeMs || now_ms - gga_ms_ > kSentenceMaxAgeMs)
        reason = FixReject::Stale;
    else if (!rmc_solution_ || !gga_solution_)
        reason = FixReject::NoSolution;
    else if (!date_ok_)
        reason = FixReject::NoDate;
    else if (jumped_)
        reason = FixReject::Jump;
    return reason;
}

}  // namespace skyblip::gnss
