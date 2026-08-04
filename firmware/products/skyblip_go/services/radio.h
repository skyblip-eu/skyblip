#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_RADIO_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_RADIO_H

#include "core/protocol/adsl_uplink.h"
#include "core/protocol/air.h"
#include "core/timing/channel.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "runtime/service.h"

namespace skyblip::go {

// Slot POLICY only: which band to listen on, from when to when, and at which
// instant of the direct slot own-ship goes on air. The dwell is executed
// against absolute deadlines by hal::Rf, whose implementation owns the hardware
// timing and the carrier sense.
class RadioService : public runtime::Service {
   public:
    using runtime::Service::Service;

    Status setup() override;
    void tick(uint32_t now_ms) override;

    hal::RfMode armed_mode() const { return armed_; }
    uint32_t arm_count() const { return arm_count_; }
    const timing::Transmitter& transmitter() const { return transmitter_; }

    // What the air discipline looks like from outside: the measured floor, the
    // threshold the next dwell will carry, how many dwells gave up without
    // getting a word in, and how much of the hour's allowance is spent.
    const timing::NoiseFloor& noise_floor() const { return noise_; }
    int8_t lbt_threshold_dbm() const { return noise_.threshold_dbm(lbt_retry_); }
    uint32_t gave_up_count() const { return transmitter_.busy_count(); }
    uint32_t duty_permille(uint32_t now_ms) const {
        return transmitter_.air_time().permille(now_ms);
    }
    bool over_budget() const { return over_budget_; }

   private:
    static constexpr uint8_t kFlightStateAirborne = 2;

    // Both from micros(), which is 64-bit and does not wrap: nothing about where
    // the radio believes it is inside the second reads the 32-bit millisecond
    // counter (hal/clock.h).
    int phase_ms() const;
    uint64_t pps_epoch_us() const;
    // Slot 1 spans the UTC second, so inside its tail the dwell, the burst it
    // carries and the second they are accounted to all belong to the second the
    // slot started in.
    uint64_t dwell_epoch_us() const;
    uint32_t slot_utc() const;
    protocol::BurstInstant burst_instant(const timing::Transmitter::Attempt& attempt,
                                         uint64_t tx_at_us) const;
    static hal::RfMode mode_for(const timing::SlotPlan& plan);
    static void listen_for(timing::Band band, hal::RfPlan& plan);
    timing::Transmitter::Attempt attempt(const timing::SlotPlan& plan, uint32_t now_ms) const;
    bool transmit_due(const timing::SlotPlan& plan, uint32_t now_ms) const;
    void arm_dwell(const timing::SlotPlan& plan, uint32_t now_ms);
    void publish_dwell(uint32_t now_ms);
    void collect_outcome(uint32_t now_ms);
    void take_carrier_samples();

    timing::Scheduler scheduler_{};
    timing::Transmitter transmitter_{};
    timing::NoiseFloor noise_{};
    // The transmit buffer, and the only writer it has: protocol::from_own, out of
    // own-ship state, the device address and the settings. There is deliberately no
    // loopback guard in front of it. SoftRF has one, because a transmission that
    // was the last frame received did happen to it ("$PSRFE,RF loopback is
    // detected on Tx", src/driver/RF.cpp:381-396), and the shape of that firmware
    // is why: one driver owning a shared Tx/Rx buffer pair, with relay and bridge
    // paths that do put received traffic back on air. Here a received frame's only
    // path is the RfEvent queue into TrafficService and the traffic table, and
    // nothing transmits from either. Adding the comparison would put work inside a
    // dwell to defend against a state this composition cannot reach; the property
    // is asserted over the air instead, in test/products/test_rf_timing.cpp.
    protocol::AdslPacket outgoing_{};
    uint8_t outgoing_chips_[protocol::kTxChipBytes]{};
    hal::RfMode armed_{hal::RfMode::Idle};
    uint32_t armed_freq_{0};
    uint32_t arm_count_{0};
    uint64_t tx_end_us_{0};
    // The deadline core/timing::Transmitter chose for the burst this dwell
    // carries: what the bench's dwell-phase histogram measures the executor's
    // report against.
    uint64_t tx_deadline_us_{0};
    uint32_t tx_utc_{0};
    uint32_t seen_tx_ok_{0};
    uint32_t seen_tx_busy_{0};
    uint32_t seen_carrier_samples_{0};
    uint8_t lbt_retry_{0};
    bool tx_armed_{false};
    bool tx_forced_{false};
    bool over_budget_{false};
};

}  // namespace skyblip::go

#endif
