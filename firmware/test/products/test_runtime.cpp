// The loop is what every shell drives, so setup failure reporting and tick order
// are pinned here rather than discovered on hardware.
#include "doctest/doctest.h"
#include "runtime/loop.h"
#include "runtime/null.h"
#include "runtime/tasks.h"
#include "runtime/watchdog.h"

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
    bool progressing(uint32_t) const override { return alive; }

    int setups{0};
    int ticks{0};
    uint32_t last_ms{0};
    bool alive{true};

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
    hal::Roles roles{clock,          null.rf,          null.link, null.display,          null.kv,
                     null.log_flash, null.annunciator, null.dfu,  hal::Capability::None, 0};
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
    hal::Roles roles{clock,          null.rf,          null.link, null.display,          null.kv,
                     null.log_flash, null.annunciator, null.dfu,  hal::Capability::None, 0};
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

// The watchdog's other half. Feeding from the bottom of a loop proves the loop
// is running and nothing else, which is exactly the failure a tracker cannot
// afford: the loop spins, one service is wedged, and the device looks alive for
// the rest of the flight. The decision is here, on the host, because on silicon
// the only way to observe it is to lose the aircraft.

TEST_CASE("watchdog: the dog is fed while every task is inside its deadline") {
    runtime::FeedDecision feed;
    const int a = feed.add("radio", 1000);
    const int b = feed.add("gnss", 1000);
    CHECK(a == 0);
    CHECK(b == 1);
    CHECK(feed.count() == 2);

    // Before the first pass there is no time base, so nothing is stale yet.
    CHECK(feed.may_feed(50000));

    feed.begin(0);
    CHECK(feed.may_feed(0));
    for (uint32_t t = 0; t < 10000; t += 100) {
        feed.check_in(a, t);
        feed.check_in(b, t);
        CHECK(feed.may_feed(t));
    }
}

TEST_CASE("watchdog: a task that stops checking in stops the feed and is named") {
    runtime::FeedDecision feed;
    const int radio = feed.add("radio", 1000);
    const int gnss = feed.add("gnss", 1000);
    feed.begin(0);

    // The radio keeps going; the GNSS task went quiet at t=0.
    for (uint32_t t = 0; t < 1000; t += 100) {
        feed.check_in(radio, t);
        CHECK(feed.may_feed(t));
    }
    feed.check_in(radio, 1000);
    CHECK_FALSE(feed.may_feed(1000));
    CHECK(feed.stalled(1000) == gnss);
    CHECK(__builtin_strcmp(feed.name(gnss), "gnss") == 0);
    CHECK(feed.silent_ms(gnss, 1000) == 1000);

    // It comes back: so does the feed. A stall is a state, not a latch, because
    // the bite is what latches it.
    feed.check_in(gnss, 1100);
    CHECK(feed.may_feed(1100));
    CHECK(feed.stalled(1100) == runtime::FeedDecision::kNone);
}

TEST_CASE("watchdog: each task carries its own rope") {
    runtime::FeedDecision feed;
    const int fast = feed.add("fast", 100);
    const int slow = feed.add("slow", 5000);
    feed.begin(0);
    feed.check_in(slow, 0);

    CHECK_FALSE(feed.may_feed(200));
    CHECK(feed.stalled(200) == fast);

    feed.check_in(fast, 200);
    CHECK(feed.may_feed(250));
}

TEST_CASE("watchdog: the loop refuses to feed for a service that is not progressing") {
    struct : hal::Clock {
        uint32_t millis() const override { return 0; }
        uint64_t micros() const override { return 0; }
    } clock;

    runtime::NullRoles null;
    hal::Roles roles{clock,          null.rf,          null.link, null.display,          null.kv,
                     null.log_flash, null.annunciator, null.dfu,  hal::Capability::None, 0};
    bus::Bus bus;
    bus::State state;
    runtime::Context ctx{roles, bus, state};

    int order[8]{0};
    Spy healthy(ctx, 1, order, Status::Ok);
    Spy wedged(ctx, 2, order, Status::Ok);
    runtime::Service* services[] = {&healthy, &wedged};
    static const char* const names[] = {"healthy", "wedged"};
    runtime::Loop loop(services, 2, names);

    // order[0] is the Spy's write cursor: rewound every pass, because this case
    // runs a thousand of them and the ordering is pinned above.
    for (uint32_t t = 0; t < runtime::kTaskWatchdogMs; t += runtime::kServiceStepMs) {
        order[0] = 0;
        loop.step(t);
        CHECK(loop.may_feed_watchdog(t));
    }

    // It keeps being ticked and keeps returning. It just is not getting
    // anywhere, and the whole point is that the loop must not vouch for it.
    wedged.alive = false;
    uint32_t t = runtime::kTaskWatchdogMs;
    for (; t < 2 * runtime::kTaskWatchdogMs; t += runtime::kServiceStepMs) {
        order[0] = 0;
        loop.step(t);
    }
    CHECK_FALSE(loop.may_feed_watchdog(t));
    CHECK(loop.stalled_service(t) == 1);
    CHECK(__builtin_strcmp(loop.feed_decision().name(loop.stalled_service(t)), "wedged") == 0);
    CHECK(wedged.ticks > 0);
}

// M. The dog's own arithmetic across the 49.7-day wrap of hal::Clock::millis().
// This is the one deadline in the tree whose failure mode is the device biting
// itself: a silence measured as 4.29 billion milliseconds is every task past every
// deadline at once, so the loop would stop feeding and the aircraft would lose its
// tracker in flight, once every seven weeks, for no reason at all.
TEST_CASE("watchdog: silence is a difference, so the wrap is not a stall") {
    runtime::FeedDecision feed;
    const int radio = feed.add("radio", 1000);
    const int gnss = feed.add("gnss", 1000);
    const uint32_t before = 0xFFFFFF00u;  // 256 ms short of the wrap
    feed.begin(before);

    // Through the wrap at the loop's own cadence: every task inside its deadline.
    for (uint32_t step = 0; step < 20; step++) {
        const uint32_t t = before + step * 50u;
        feed.check_in(radio, t);
        feed.check_in(gnss, t);
        CHECK(feed.may_feed(t));
        CHECK(feed.silent_ms(radio, t) == 0);
    }

    // A task that went quiet 256 ms before the wrap is 1000 ms quiet 744 ms after
    // it, and not one millisecond more.
    feed.check_in(gnss, before);
    feed.check_in(radio, 743u);
    CHECK(feed.silent_ms(gnss, 744u) == 1000);
    CHECK(feed.may_feed(743u));
    CHECK_FALSE(feed.may_feed(744u));
    CHECK(feed.stalled(744u) == gnss);
    // The one that is still checking in is not the one named.
    CHECK(feed.silent_ms(radio, 744u) == 1);
}
