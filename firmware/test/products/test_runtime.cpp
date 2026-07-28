// The loop is what every shell drives, so setup failure reporting and tick order
// are pinned here rather than discovered on hardware.
#include "doctest/doctest.h"
#include "runtime/loop.h"
#include "runtime/null.h"

using namespace skyblip;

namespace {

struct Spy : runtime::Service {
    Spy(runtime::Context& ctx, int id, int* order, Status setup_status)
        : runtime::Service(ctx), id_(id), order_(order), status_(setup_status) {}

    Status setup() override {
        setups++;
        return status_;
    }
    void tick(uint32_t now_ms) override {
        ticks++;
        last_ms = now_ms;
        order_[(*order_)++ + 1] = id_;
    }

    int setups{0};
    int ticks{0};
    uint32_t last_ms{0};

   private:
    int id_;
    int* order_;
    Status status_;
};

struct Fixture {
    runtime::NullRoles null;
    hal::Clock* clock{nullptr};
    bus::Bus bus;
    bus::State state;
};

}  // namespace

TEST_CASE("runtime: the loop sets up every service and ticks them in order") {
    struct : hal::Clock {
        uint32_t millis() const override { return 0; }
        uint64_t micros() const override { return 0; }
    } clock;

    runtime::NullRoles null;
    hal::Roles roles{clock,   null.rf,          null.link, null.display,
                     null.kv, null.annunciator, null.dfu,  hal::Capability::None,
                     0};
    bus::Bus bus;
    bus::State state;
    runtime::Context ctx{roles, bus, state};

    int order[8]{0};
    Spy a(ctx, 1, order, Status::Ok);
    Spy b(ctx, 2, order, Status::Ok);
    runtime::Service* services[] = {&a, &b};
    runtime::Loop loop(services, 2);

    CHECK(loop.setup() == Status::Ok);
    CHECK(a.setups == 1);
    CHECK(b.setups == 1);

    loop.step(1234);
    CHECK(a.ticks == 1);
    CHECK(b.ticks == 1);
    CHECK(a.last_ms == 1234);
    CHECK(order[1] == 1);
    CHECK(order[2] == 2);
}

TEST_CASE("runtime: setup reports the first failure but still sets up the rest") {
    struct : hal::Clock {
        uint32_t millis() const override { return 0; }
        uint64_t micros() const override { return 0; }
    } clock;

    runtime::NullRoles null;
    hal::Roles roles{clock,   null.rf,          null.link, null.display,
                     null.kv, null.annunciator, null.dfu,  hal::Capability::None,
                     0};
    bus::Bus bus;
    bus::State state;
    runtime::Context ctx{roles, bus, state};

    int order[8]{0};
    Spy failing(ctx, 1, order, Status::Down);
    Spy ok(ctx, 2, order, Status::Ok);
    runtime::Service* services[] = {&failing, &ok};
    runtime::Loop loop(services, 2);

    CHECK(loop.setup() == Status::Down);
    CHECK(ok.setups == 1);
}

TEST_CASE("runtime: a null role accepts every call and reports nothing works") {
    runtime::NullRoles null;
    CHECK(null.rf.begin() == Status::Down);
    CHECK(null.rf.arm(hal::RfPlan{}) == Status::Down);
    CHECK(null.link.send(messages::Endpoint::Nmea, ConstByteSpan{}) == Status::Down);
    size_t n = 0;
    uint8_t buf[4];
    CHECK(null.kv.read("k", buf, sizeof(buf), n) == Status::NotFound);
    null.annunciator.alarm(3, 3);  // must not crash: absent hardware is silent
    null.display.power_off();
}
