#include "products/skyblip_go/services/radio.h"

namespace skyblip::go {

Status RadioService::setup() {
    if (!hal::has(context_.roles.capabilities, hal::Capability::Rf)) return Status::Down;
    arm(hal::RfMode::RxMband, 0);
    return Status::Ok;
}

void RadioService::tick(uint32_t now_ms) {
    context_.state.plan = scheduler_.plan(phase_ms(now_ms), context_.state.clock);
    const hal::RfMode want = mode_for(context_.state.plan);
    if (want != armed_) arm(want, now_ms);
}

int RadioService::phase_ms(uint32_t now_ms) const {
    if (context_.state.clock.pps_locked)
        return static_cast<int>(context_.state.clock.ms_since_pps % 1000);
    return static_cast<int>(now_ms % 1000);
}

uint64_t RadioService::pps_epoch_us(uint32_t now_ms) const {
    const uint64_t now_us = context_.roles.clock.micros();
    const uint64_t phase_us = static_cast<uint64_t>(phase_ms(now_ms)) * 1000;
    return now_us > phase_us ? now_us - phase_us : 0;
}

hal::RfMode RadioService::mode_for(const timing::SlotPlan& plan) {
    return plan.band == timing::Band::O ? hal::RfMode::RxOband : hal::RfMode::RxMband;
}

void RadioService::arm(hal::RfMode mode, uint32_t now_ms) {
    const uint64_t epoch_us = pps_epoch_us(now_ms);
    hal::RfPlan plan{};
    plan.mode = mode;
    if (mode == hal::RfMode::RxOband) {
        plan.freq_hz = kObandFreqHz;
        plan.start_us = epoch_us + timing::kUplinkRxStart * 1000ull;
        plan.end_us = epoch_us + timing::kUplinkRxEnd * 1000ull;
    } else {
        plan.freq_hz = kMbandFreqHz;
        plan.start_us = epoch_us + timing::kSlot0Start * 1000ull;
        plan.end_us = epoch_us + timing::kSlot1End * 1000ull;
    }
    // Arming inside the window means the dwell has already started: begin now and
    // keep the same hard stop, rather than waiting a whole second for the next one.
    const uint64_t now_us = context_.roles.clock.micros();
    if (plan.start_us < now_us) plan.start_us = now_us;
    if (plan.end_us <= plan.start_us) plan.end_us = plan.start_us + 1000;

    if (context_.roles.rf.arm(plan) == Status::Ok) {
        armed_ = mode;
        arm_count_++;
    }
}

}  // namespace skyblip::go
