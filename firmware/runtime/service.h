#ifndef SKYBLIP_RUNTIME_SERVICE_H
#define SKYBLIP_RUNTIME_SERVICE_H

#include "core/bus/bus.h"
#include "core/bus/state.h"
#include "core/util/result.h"
#include "hal/roles.h"

namespace skyblip::runtime {

struct Context {
    hal::Roles& roles;
    bus::Bus& bus;
    bus::State& state;
};

class Service {
   public:
    explicit Service(Context& context) : context_(context) {}
    virtual ~Service() = default;

    virtual Status setup() { return Status::Ok; }
    virtual void tick(uint32_t now_ms) = 0;

    // Whether this service made progress this pass. The loop feeds the watchdog
    // only on behalf of services that say yes, so a service that knows it is
    // wedged takes the device down rather than riding along on a loop that is
    // still spinning. Default: reaching tick() is progress enough.
    virtual bool progressing(uint32_t /*now_ms*/) const { return true; }

   protected:
    Context& context_;
};

}  // namespace skyblip::runtime

#endif
