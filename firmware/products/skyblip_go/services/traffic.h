#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_TRAFFIC_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_TRAFFIC_H

#include "runtime/service.h"

namespace skyblip::go {

// Owns state.traffic: radio events become targets. A frame that fails CRC after
// forward correction is counted and dropped, never shown.
class TrafficService : public runtime::Service {
   public:
    using runtime::Service::Service;

    void tick(uint32_t now_ms) override;

   private:
    void on_frame(const messages::RfEvent& event, uint32_t now_ms);
};

}  // namespace skyblip::go

#endif
