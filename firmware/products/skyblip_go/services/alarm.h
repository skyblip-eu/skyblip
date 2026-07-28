#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_ALARM_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_ALARM_H

#include "runtime/service.h"

namespace skyblip::go {

class AlarmService : public runtime::Service {
   public:
    using runtime::Service::Service;

    void tick(uint32_t now_ms) override;

    bool escalated_since_render() const { return dirty_; }
    void clear_dirty() { dirty_ = false; }

   private:
    // core/traffic/alarm.h grades contacts 1 info, 2 important, 3 urgent.
    static constexpr uint8_t kVibroFromLevel = 2;
    static constexpr uint8_t kUrgentLevel = 3;
    // Long enough to feel through a glove and a harness strap, short enough not
    // to blur into the next escalation.
    static constexpr uint16_t kVibroImportantMs = 200;
    static constexpr uint16_t kVibroUrgentMs = 600;

    bool dirty_{false};
};

}  // namespace skyblip::go

#endif
