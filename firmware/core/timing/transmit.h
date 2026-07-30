// Own-ship transmit policy: ADS-L 4 SRD-860 issue 2 §D.3 (CSMA, listen before
// talk, forced transmission) and §G.1.16 (transmit rate, navigation age).
#ifndef SKYBLIP_CORE_TIMING_TRANSMIT_H
#define SKYBLIP_CORE_TIMING_TRANSMIT_H

#include "core/timing/slot.h"

namespace skyblip::timing {

class Transmitter {
   public:
    // §C.2 at 100 kchip/s: 16-chip preamble, 64-chip Manchester sync word, then
    // 25 Manchester-encoded bytes = 4.8 ms, rounded up.
    static constexpr uint32_t kAirTimeMs = 5;
    // §D.3: when no packet could be transmitted 3000 ms after the first
    // attempt, transmit irrespective of carrier detect, then stay off air.
    static constexpr uint32_t kForceAfterMs = 3000;
    static constexpr uint32_t kQuietAfterForcedMs = 2000;
    // §G.1.16: at least 1 Hz airborne, 0.1 Hz on the ground.
    static constexpr uint32_t kGroundPeriodMs = 10000;
    static constexpr uint32_t kFixAgeMaxMs = 500;
    // Ours, not the spec's: §C.5 gives the direct slot 450..1000 and requires a
    // burst to complete before the slot ends. Margin between the burst's last
    // chip and the end of the window, absorbing PPS error, the carrier sample
    // and the SPI write. Nothing is owed at the front: a dwell that has opened
    // is tuned, the retune was paid for by the guard before it.
    static constexpr int kCompletionSlackMs = 5;
    // §C.2 backoff interval, applied by the executor between carrier samples.
    static constexpr uint32_t kBackoffMinMs = 15;
    static constexpr uint32_t kBackoffMaxMs = 250;
    static constexpr int8_t kBusyThresholdDbm = -90;

    struct Attempt {
        bool go{false};
        int at_ms{0};
        uint32_t freq_hz{0};
        bool force{false};
    };

    void configure(uint32_t device_addr) { addr_ = device_addr; }

    // The instant this device transmits in the second `utc`, or go=false. Pure:
    // calling it twice with the same arguments gives the same answer.
    Attempt attempt(const SlotPlan& plan, uint32_t utc, uint32_t now_ms, bool airborne,
                    uint32_t fix_age_ms) const;

    void sent(uint32_t utc, uint32_t now_ms, bool forced);
    void busy(uint32_t now_ms);

    uint32_t sent_count() const { return sent_; }
    uint32_t busy_count() const { return busy_; }
    // §C.2.5: traffic alternates between the two M-band channels, so the slot
    // to transmit in follows the transmission count, not the clock.
    int next_slot() const { return static_cast<int>(sent_ & 1u); }

   private:
    // Uniform over the slot's usable width and decorrelated between devices:
    // two aircraft with different addresses do not collide every second, and
    // one aircraft's instant is reproducible in a test.
    int instant_in(int slot, uint32_t utc) const;

    uint32_t addr_{0};
    uint32_t sent_{0};
    uint32_t busy_{0};
    uint32_t last_sent_ms_{0};
    uint32_t last_sent_utc_{0};
    uint32_t first_attempt_ms_{0};
    uint32_t quiet_until_ms_{0};
    bool ever_sent_{false};
};

}  // namespace skyblip::timing

#endif
