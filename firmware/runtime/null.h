#ifndef SKYBLIP_RUNTIME_NULL_H
#define SKYBLIP_RUNTIME_NULL_H

#include "hal/annunciator.h"
#include "hal/dfu.h"
#include "hal/display.h"
#include "hal/kvstore.h"
#include "hal/link.h"
#include "hal/rf.h"

namespace skyblip::runtime {

class NullDisplay : public hal::Display {
   public:
    void present(const ui::Framebuffer&, hal::Rect, hal::Refresh) override {}
    void power_off() override {}
};

class NullAnnunciator : public hal::Annunciator {
   public:
    void alarm(uint8_t, uint8_t) override {}
    void vibrate(uint16_t) override {}
    void silence() override {}
};

class NullLink : public hal::Link {
   public:
    Status send(messages::Endpoint, ConstByteSpan) override { return Status::Down; }
};

class NullKvStore : public hal::KvStore {
   public:
    Status read(const char*, uint8_t*, size_t, size_t&) override { return Status::NotFound; }
    Status write(const char*, const uint8_t*, size_t) override { return Status::Down; }
    Status erase(const char*) override { return Status::Ok; }
};

class NullDfu : public hal::Dfu {
   public:
    void trigger() override {}
};

class NullRf : public hal::Rf {
   public:
    Status begin() override { return Status::Down; }
    Status arm(const hal::RfPlan&) override { return Status::Down; }
    void abort() override {}
};

struct NullRoles {
    NullDisplay display;
    NullAnnunciator annunciator;
    NullLink link;
    NullKvStore kv;
    NullDfu dfu;
    NullRf rf;
};

}  // namespace skyblip::runtime

#endif
