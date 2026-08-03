#include "core/gnss/first_fix.h"

namespace skyblip::gnss {

void FirstFix::update(bool fix_valid, uint32_t now_ms) {
    if (fix_valid == has_fix_) return;
    has_fix_ = fix_valid;
    if (!fix_valid) return;

    settle_ms_ = ever_fixed_ ? kRefixSettleMs : kFirstFixSettleMs;
    acquired_pending_ = !ever_fixed_;
    ever_fixed_ = true;
    fix_since_ms_ = now_ms;
}

bool FirstFix::take_acquired() {
    const bool acquired = acquired_pending_;
    acquired_pending_ = false;
    return acquired;
}

bool FirstFix::settled(uint32_t now_ms) const {
    if (!has_fix_) return false;
    return now_ms - fix_since_ms_ >= settle_ms_;
}

}  // namespace skyblip::gnss
