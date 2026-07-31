#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_TRAFFIC_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_TRAFFIC_H

#include "core/protocol/air.h"
#include "runtime/service.h"

namespace skyblip::go {

// Owns state.traffic: radio events become targets. One M-band dwell reports both
// systems, so the frame names itself before it is decoded, and a frame that
// fails CRC after forward correction is counted and dropped, never shown.
class TrafficService : public runtime::Service {
   public:
    using runtime::Service::Service;

    void tick(uint32_t now_ms) override;

   private:
    void on_frame(const messages::RfEvent& event, uint32_t now_ms);
    static bool decode_adsl(protocol::Frame& frame, uint32_t utc, messages::AircraftObs& obs);
    bool decode_alptas(const protocol::Frame& frame, uint32_t utc,
                       messages::AircraftObs& obs) const;
};

}  // namespace skyblip::go

#endif
