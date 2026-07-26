#include "core/traffic/alarm.h"
#include "core/traffic/table.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::traffic;

static messages::AircraftObs obs(uint32_t addr, uint8_t tbl, uint32_t t,
                                 messages::Source src = messages::Source::AdslDirect) {
    messages::AircraftObs o{};
    o.addr = addr;
    o.addr_table = tbl;
    o.rx_utc = t;
    o.source = src;
    o.valid_pos = true;
    o.lat_1e7 = 481000000;
    o.lon_1e7 = 81000000;
    o.alt_m = 1000;
    return o;
}

TEST_CASE("traffic: insert, find, count") {
    TrafficTable tbl;
    CHECK(tbl.count() == 0);
    int i = tbl.update(obs(0x111, 6, 100), 100);
    CHECK(i >= 0);
    CHECK(tbl.count() == 1);
    CHECK(tbl.find(6, 0x111) == i);
    CHECK(tbl.find(6, 0x222) == -1);
}

TEST_CASE("traffic: dedup merges same target; fresher/direct wins") {
    TrafficTable tbl;
    tbl.update(obs(0x111, 6, 100, messages::Source::AdslUplink), 100);
    // direct, same time -> should replace uplink (direct preferred)
    tbl.update(obs(0x111, 6, 100, messages::Source::AdslDirect), 100);
    CHECK(tbl.count() == 1);
    int idx = tbl.find(6, 0x111);
    CHECK(tbl.at(idx)->obs.source == messages::Source::AdslDirect);
    // older observation must not overwrite a fresher one
    tbl.update(obs(0x111, 6, 90, messages::Source::AdslUplink), 100);
    CHECK(tbl.at(idx)->obs.rx_utc == 100);
}

TEST_CASE("traffic: age-out removes stale entries") {
    TrafficTable tbl;
    tbl.update(obs(0x1, 6, 100), 100);
    tbl.update(obs(0x2, 6, 120), 120);
    tbl.age_out(140, 30);  // 0x1 is 40 s old -> gone; 0x2 is 20 s -> stays
    CHECK(tbl.find(6, 0x1) == -1);
    CHECK(tbl.find(6, 0x2) >= 0);
}

TEST_CASE("traffic: overflow drops oldest non-threat, keeps active alarms") {
    TrafficTable tbl;
    // fill capacity
    for (int i = 0; i < TrafficTable::kCapacity; i++) {
        int idx = tbl.update(obs(0x1000 + i, 6, 100 + i), 200);
        REQUIRE(idx >= 0);
    }
    // mark the oldest entry as an active alarm so it can't be evicted
    int oldest = tbl.find(6, 0x1000);
    tbl.at(oldest)->alarm_level = 3;
    int idx = tbl.update(obs(0x9999, 6, 300), 300);
    CHECK(idx >= 0);                  // newcomer placed
    CHECK(tbl.find(6, 0x1000) >= 0);  // protected alarm still present
}

TEST_CASE("alarm: level escalates as a target closes head-on") {
    messages::OwnState own{};
    own.fix_valid = true;
    own.lat_1e7 = 481000000;
    own.lon_1e7 = 81000000;
    own.alt_m = 1000;
    own.speed_q = 40 * 4;  // 40 m/s
    own.track_c9 = 0;      // north

    auto target_at = [&](int north_m, int up_m) {
        messages::AircraftObs t{};
        t.valid_pos = true;
        t.has_speed = true;
        t.speed_q = 40 * 4;
        t.alt_m = own.alt_m + up_m;
        // place target `north_m` north of own: dLat = north_m / 0.011132
        t.lat_1e7 = own.lat_1e7 + static_cast<int32_t>((int64_t)north_m * 1000000 / 11132);
        t.lon_1e7 = own.lon_1e7;
        return t;
    };

    CHECK(assess(own, target_at(5000, 0)).level <= 1);  // far
    CHECK(assess(own, target_at(1200, 0)).level >= 2);  // important
    CHECK(assess(own, target_at(300, 0)).level == 3);   // urgent
    // large vertical separation suppresses the alarm
    CHECK(assess(own, target_at(300, 800)).level == 0);
}

TEST_CASE("alarm: invalid when own has no fix") {
    messages::OwnState own{};
    messages::AircraftObs t{};
    t.valid_pos = true;
    CHECK_FALSE(assess(own, t).valid);
}
