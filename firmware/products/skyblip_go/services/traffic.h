#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_TRAFFIC_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_TRAFFIC_H

#include "core/protocol/adsl_uplink.h"
#include "core/protocol/air.h"
#include "products/skyblip_go/features.h"
#include "runtime/service.h"

namespace skyblip::go {

// Owns state.traffic: radio events become targets. The dwell that heard a burst
// names the system it belongs to (core/protocol/air.h), so nothing here guesses:
// one M-band dwell reports two systems and the frame names itself out of its
// sync tail, while a burst from the O-band dwell is a ground station's uplink
// frame. A frame that fails CRC after forward correction is counted and dropped,
// never shown.
class TrafficService : public runtime::Service {
   public:
    TrafficService(runtime::Context& context, Feature declared)
        : runtime::Service(context), uplink_(has_feature(declared, Feature::UplinkRx)) {}

    Status setup() override;
    void tick(uint32_t now_ms) override;

    bool uplink_enabled() const { return uplink_; }

   private:
    void on_frame(const messages::RfEvent& event, uint32_t now_ms);
    void on_uplink(const messages::RfEvent& event, uint32_t now_ms);
    static bool decode_adsl(protocol::Frame& frame, uint32_t utc, messages::AircraftObs& obs);
    bool decode_alptas(const protocol::Frame& frame, uint32_t utc,
                       messages::AircraftObs& obs) const;

    protocol::AdslUplink uplink_codec_{};
    const bool uplink_;
};

}  // namespace skyblip::go

#endif
