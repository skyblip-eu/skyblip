#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_OWNSHIP_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_OWNSHIP_H

#include "runtime/service.h"

namespace skyblip::go {

// Owns state.own: sensor readings become the one state the protocol encoder, the
// alarm logic and every screen read.
class OwnshipService : public runtime::Service {
   public:
    using runtime::Service::Service;

    void tick(uint32_t now_ms) override;

    bool baro_active() const { return baro_ref_ms_ != 0; }

   private:
    void apply_fix(const gnss::GnssFix& fix, uint32_t now_ms);
    void apply_baro(const messages::BaroSample& sample);
    void update_turn_rate(uint32_t now_ms);
    bool vs_from_alt_cm(int32_t alt_cm, uint32_t now_ms, uint32_t window_ms, int32_t& ref_alt_cm,
                        uint32_t& ref_ms, int16_t& out_e8) const;

    int32_t vs_ref_alt_cm_{0};
    uint32_t vs_ref_ms_{0};
    int32_t baro_ref_alt_cm_{0};
    uint32_t baro_ref_ms_{0};
    uint32_t turn_ref_ms_{0};
    uint16_t turn_ref_track_c9_{0};

    static constexpr uint32_t kVsWindowMs = 2000;
    // Pressure is far quieter than differentiated GNSS altitude, so the same
    // confidence needs a shorter window - which is the point of having a baro.
    static constexpr uint32_t kBaroVsWindowMs = 1000;
    static constexpr uint32_t kTurnWindowMs = 1000;
};

}  // namespace skyblip::go

#endif
