#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_RADIO_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_RADIO_H

#include "core/timing/slot.h"
#include "runtime/service.h"

namespace skyblip::go {

// Slot POLICY only: which band to listen on, from when to when. The dwell is
// executed against absolute deadlines by hal::Rf, whose implementation owns the
// hardware timing.
class RadioService : public runtime::Service {
   public:
    using runtime::Service::Service;

    // ADS-L 4 SRD-860 issue 2: M-band direct 868.2 MHz, O-band HDR uplink
    // 869.525 MHz.
    static constexpr uint32_t kMbandFreqHz = 868200000;
    static constexpr uint32_t kObandFreqHz = 869525000;

    Status setup() override;
    void tick(uint32_t now_ms) override;

    hal::RfMode armed_mode() const { return armed_; }
    uint32_t arm_count() const { return arm_count_; }

   private:
    int phase_ms(uint32_t now_ms) const;
    uint64_t pps_epoch_us(uint32_t now_ms) const;
    static hal::RfMode mode_for(const timing::SlotPlan& plan);
    void arm(hal::RfMode mode, uint32_t now_ms);

    timing::Scheduler scheduler_{};
    hal::RfMode armed_{hal::RfMode::Idle};
    uint32_t arm_count_{0};
};

}  // namespace skyblip::go

#endif
