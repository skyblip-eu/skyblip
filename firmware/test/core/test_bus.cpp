// The bus is the only path between a producer and a service, so its overflow
// behaviour is a safety property: a dropped message must be counted, never
// silently swallowed, and the queue must not corrupt when it wraps.
#include "core/bus/bus.h"
#include "core/bus/state.h"
#include "doctest/doctest.h"

using namespace skyblip;

TEST_CASE("bus: a queue round-trips in order") {
    bus::Queue<int, 4> q;
    CHECK(q.empty());
    for (int i = 1; i <= 4; i++) CHECK(q.push(i));

    int out = 0;
    for (int i = 1; i <= 4; i++) {
        REQUIRE(q.pop(out));
        CHECK(out == i);
    }
    CHECK(q.empty());
    CHECK_FALSE(q.pop(out));
}

TEST_CASE("bus: pushing past capacity drops the new item and counts it") {
    bus::Queue<int, 2> q;
    CHECK(q.push(1));
    CHECK(q.push(2));
    CHECK_FALSE(q.push(3));
    CHECK(q.dropped() == 1);

    int out = 0;
    REQUIRE(q.pop(out));
    CHECK(out == 1);  // the OLD items survive: a queue is not a ring buffer here
    CHECK(q.push(4));
}

TEST_CASE("bus: indices wrap without losing a slot") {
    bus::Queue<int, 3> q;
    int out = 0;
    for (int round = 0; round < 10; round++) {
        CHECK(q.push(round));
        REQUIRE(q.pop(out));
        CHECK(out == round);
    }
    CHECK(q.dropped() == 0);
}

TEST_CASE("bus: the traffic clock falls back to uptime before the first UTC") {
    bus::State s;
    s.own.utc_valid = false;
    CHECK(s.traffic_now(12500) == 12);
    s.own.utc_valid = true;
    s.own.utc = 43200;
    CHECK(s.traffic_now(12500) == 43200);
}
