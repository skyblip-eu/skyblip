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

   protected:
    Context& context_;
};

}  // namespace skyblip::runtime

#endif
