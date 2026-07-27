// test/products/test_simulator.cpp — end-to-end tests through the simulator.
// These are the strongest tests in the suite: simulated GNSS emits real NMEA
// that the production parser decodes, and virtual aircraft are encoded as real
// ADS-L frames that the production receive path (CRC → descramble → to_obs)
// decodes into the traffic table and the collision alarm. No mocks of logic.
#include <cstdlib>

#include "doctest/doctest.h"
#include "products/skyblip_go/simulator/t_echo_plus.h"

using namespace skyblip;

namespace {
// Run the virtual board forward from `from` to `to` ms in 50 ms slices.
void run(simulator::TEchoPlus& h, uint32_t from, uint32_t to) {
    for (uint32_t t = from; t <= to; t += 50) h.step(t);
}
}  // namespace

TEST_CASE("sim: simulated GNSS drives own-ship state via the real NMEA parser") {
    simulator::TEchoPlus h;
    REQUIRE(h.setup() == Status::Ok);
    h.set_fix(true);
    h.set_sats(11);
    h.set_altitude_m(1500);
    h.set_speed_kt(50);
    h.set_track_deg(90);
    h.gnss().lat_1e7 = 485000000;
    h.gnss().lon_1e7 = 85000000;

    run(h, 0, 3000);

    CHECK(h.own().fix_valid);
    CHECK(h.own().utc_valid);
    CHECK(int(h.own().sats) == 11);
    CHECK(h.own().alt_m == 1500);
    CHECK(int(h.own().track_c9) == 128);  // 90 deg in cordic9
    CHECK(h.own().speed_q > 90);          // 50 kt ~ 25.7 m/s -> ~103 quarter-m/s
    CHECK(h.own().lat_1e7 > 484000000);
    CHECK(h.own().lon_1e7 > 85000000);  // moved east on track 090
}

TEST_CASE("sim: losing the fix clears own-ship validity (fail closed)") {
    simulator::TEchoPlus h;
    REQUIRE(h.setup() == Status::Ok);
    run(h, 0, 2000);
    REQUIRE(h.own().fix_valid);

    h.set_fix(false);
    run(h, 2000, 5000);
    CHECK_FALSE(h.own().fix_valid);
}

TEST_CASE("sim: a virtual aircraft arrives as a real ADS-L frame and enters traffic") {
    simulator::TEchoPlus h;
    REQUIRE(h.setup() == Status::Ok);
    run(h, 0, 2000);
    REQUIRE(h.own().fix_valid);
    CHECK(h.traffic_count() == 0);

    h.add_aircraft(2000, 0, 0);  // 2 km north
    run(h, 2000, 4000);

    CHECK(h.rx_ok() > 0);           // frames actually decoded (CRC ok)
    CHECK(h.rx_bad() == 0);         // and none corrupt
    CHECK(h.traffic_count() >= 1);  // fused into the table
}

TEST_CASE("sim: a converging aircraft raises the collision alarm and buzzer") {
    simulator::TEchoPlus h;
    REQUIRE(h.setup() == Status::Ok);
    run(h, 0, 2000);
    REQUIRE(h.own().fix_valid);
    CHECK(int(h.alarm_level()) == 0);

    h.add_threat();  // ~600 m, converging, co-altitude
    run(h, 2000, 4000);

    CHECK(h.traffic_count() >= 1);
    CHECK(int(h.alarm_level()) > 0);  // annunciator was driven
}

TEST_CASE("sim: clearing traffic empties the table and silences the alarm") {
    simulator::TEchoPlus h;
    REQUIRE(h.setup() == Status::Ok);
    run(h, 0, 2000);
    h.add_threat();
    run(h, 2000, 4000);
    REQUIRE(h.traffic_count() >= 1);

    h.clear_aircraft();
    run(h, 4000, 6000);
    CHECK(h.traffic_count() == 0);
    CHECK(int(h.alarm_level()) == 0);
}

TEST_CASE("sim: every page renders ink to the panel") {
    simulator::TEchoPlus h;
    REQUIRE(h.setup() == Status::Ok);
    h.add_aircraft(1500, 500, 50);
    run(h, 0, 2500);

    for (int i = 0; i < static_cast<int>(go::Page::kCount); i++) {
        run(h, 2500 + static_cast<uint32_t>(i) * 1500, 3500 + static_cast<uint32_t>(i) * 1500);
        CHECK(h.framebuffer().count_black() > 20);  // something was drawn
        h.button();
    }
    CHECK(h.present_count() > 0);
}
