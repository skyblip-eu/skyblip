// End-to-end tests through the simulator: modelled GNSS emits real NMEA that the
// production parser decodes, and virtual aircraft are encoded as real ADS-L
// frames that the production receive path decodes into the traffic table and the
// collision alarm. No mocks of logic.
#include <cstdlib>

#include "core/flight/atmosphere.h"
#include "doctest/doctest.h"
#include "simulator/simulator.h"

using namespace skyblip;

namespace {

void run(simulator::Simulator& h, uint32_t from, uint32_t to) {
    for (uint32_t t = from; t <= to; t += simulator::Simulator::kStepMs) h.step(t);
}

// A press has to outlast ui::Button's debounce window, so it is held across
// steps exactly as a thumb would hold it.
uint32_t press(simulator::Simulator& h, uint32_t t) {
    h.world().press_button();
    for (int i = 0; i < 5; i++) {
        h.step(t);
        t += 40;
    }
    return t;
}

}  // namespace

TEST_CASE("simulator: simulated GNSS drives own-ship state via the real NMEA parser") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_sats(11);
    h.world().set_altitude_m(1500);
    h.world().set_speed_kt(50);
    h.world().set_track_deg(90);
    h.world().gnss().lat_1e7 = 485000000;
    h.world().gnss().lon_1e7 = 85000000;

    run(h, 0, 3000);

    CHECK(h.product().state().own.fix_valid);
    CHECK(h.product().state().own.utc_valid);
    CHECK(int(h.product().state().own.sats) == 11);
    CHECK(h.product().state().own.alt_m == 1500);
    CHECK(int(h.product().state().own.track_c9) == 128);  // 90 deg in cordic9
    CHECK(h.product().state().own.speed_q > 90);          // 50 kt ~ 25.7 m/s -> ~103 quarter-m/s
    CHECK(h.product().state().own.lat_1e7 > 484000000);
    CHECK(h.product().state().own.lon_1e7 > 85000000);  // moved east on track 090
}

TEST_CASE("simulator: losing the fix clears own-ship validity (fail closed)") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    run(h, 0, 2000);
    REQUIRE(h.product().state().own.fix_valid);

    h.world().set_fix(false);
    run(h, 2000, 5000);
    CHECK_FALSE(h.product().state().own.fix_valid);
}

TEST_CASE("simulator: a virtual aircraft arrives as a real ADS-L frame and enters traffic") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    run(h, 0, 2000);
    REQUIRE(h.product().state().own.fix_valid);
    CHECK(h.product().state().traffic.count() == 0);

    h.world().add_aircraft(2000, 0, 0);  // 2 km north
    run(h, 2000, 4000);

    CHECK(h.product().state().rx_ok > 0);             // frames actually decoded (CRC ok)
    CHECK(h.product().state().rx_bad == 0);           // and none corrupt
    CHECK(h.product().state().traffic.count() >= 1);  // fused into the table
}

TEST_CASE("simulator: a converging aircraft raises the collision alarm and buzzer") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    run(h, 0, 2000);
    REQUIRE(h.product().state().own.fix_valid);
    CHECK(int(h.alarm_level()) == 0);

    h.world().add_threat();  // ~600 m, converging, co-altitude
    run(h, 2000, 4000);

    CHECK(h.product().state().traffic.count() >= 1);
    CHECK(int(h.alarm_level()) > 0);  // annunciator was driven
}

TEST_CASE("simulator: clearing traffic empties the table and silences the alarm") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    run(h, 0, 2000);
    h.world().add_threat();
    run(h, 2000, 4000);
    REQUIRE(h.product().state().traffic.count() >= 1);

    // Nothing transmits any more, so the targets age out of the table on their
    // own schedule (core/traffic 30 s) rather than being deleted behind its back.
    h.world().clear_aircraft();
    run(h, 4000, 40000);
    CHECK(h.product().state().traffic.count() == 0);
    CHECK(int(h.alarm_level()) == 0);
}

TEST_CASE("simulator: every page renders ink to the panel") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().add_aircraft(1500, 500, 50);
    run(h, 0, 2500);

    uint32_t t = 2500;
    for (int i = 0; i < static_cast<int>(go::Page::kCount); i++) {
        run(h, t, t + 1000);
        t += 1000;
        CHECK(h.panel().count_black() > 20);  // something was drawn
        t = press(h, t);
    }
    CHECK(h.present_count() > 0);
}

TEST_CASE("simulator: a modelled turn deflects the six-pack turn coordinator") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(100);
    h.world().set_track_deg(0);
    run(h, 0, 2000);
    uint32_t page_t = 2000;
    page_t = press(h, page_t);  // radar -> 6-pack
    run(h, page_t, page_t + 2000);
    REQUIRE(h.product().screen().page() == go::Page::SixPack);
    const ui::Framebuffer level = h.panel();

    // A standard-rate turn: 3 deg/s of track change, held four seconds.
    uint32_t t = page_t + 2000;
    for (int i = 1; i <= 4; i++) {
        h.world().set_track_deg(i * 3);
        run(h, t, t + 1000);
        t += 1000;
    }

    int diff = 0;  // the turn tile: centre (34, 138), radius 29
    for (int y = 109; y <= 167; y++)
        for (int x = 5; x <= 63; x++)
            if (level.get_pixel(x, y) != h.panel().get_pixel(x, y)) diff++;
    CHECK(diff > 10);
}

TEST_CASE("simulator: a modelled climb reaches own-ship state through the barometer") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_altitude_m(1000);
    h.world().set_climb_e1(30);  // +3.0 m/s, integrated by the GNSS model's altitude
    run(h, 0, 6000);

    // Both sensors see the same air, and the barometer is what publishes the rate.
    CHECK(h.product().state().own.climb_e8 > 16);  // more than 2 m/s
    CHECK(h.product().state().own.climb_e8 < 40);  // and less than 5 m/s
    // above sea level
    CHECK(h.world().baro().pressure_pa() < flight::kIsaSeaLevelPa);

    h.world().set_climb_e1(-30);  // now sinking
    run(h, 6000, 14000);
    CHECK(h.product().state().own.climb_e8 < 0);
}

TEST_CASE("simulator: an escalating threat buzzes and, from 'important', vibrates") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    run(h, 0, 2000);
    REQUIRE(h.product().state().own.fix_valid);
    REQUIRE(h.vibro_ms() == 0);

    // A distant contact: info level only. Audible, but it must NOT buzz the
    // motor - a pilot who feels every passing glider stops feeling anything.
    h.world().add_aircraft(2500, 0, 0, 20, 90);
    run(h, 2000, 5000);
    if (h.alarm_level() == 1) CHECK(h.vibro_ms() == 0);

    // Now something close and converging: important or urgent, so it must vibrate.
    h.world().add_threat();
    run(h, 5000, 9000);
    REQUIRE(h.alarm_level() >= 2);
    CHECK(h.vibro_ms() >= 200);
}

TEST_CASE("simulator: a threat going away does not buzz the motor again") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    run(h, 0, 2000);
    h.world().add_threat();
    run(h, 2000, 6000);
    REQUIRE(h.alarm_level() >= 2);
    REQUIRE(h.vibro_ms() >= 200);

    // De-escalation is a level CHANGE too, and it must not be mistaken for a new
    // threat: the annunciator records the last duration, so a fresh pulse would
    // show up as a change here.
    const uint16_t after_escalation = h.vibro_ms();
    h.world().clear_aircraft();
    run(h, 6000, 40000);
    CHECK(h.alarm_level() == 0);
    CHECK(h.vibro_ms() == after_escalation);  // unchanged: no pulse on the way down
}

// The same call the page's "+ Aircraft" button and the terminal's [j] make. If
// this passes and the page still shows nothing, the gap is in the shell, not in
// the receive path.
TEST_CASE("simulator: an ALP-TAS-equipped aircraft enters traffic as ALP-TAS") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    run(h, 0, 2000);
    REQUIRE(h.product().state().own.fix_valid);

    h.world().add_aircraft(2000, 0, 0, 30, 270, -1, -1, protocol::System::Alptas);
    run(h, 2000, 5000);

    const traffic::TrafficTable& table = h.product().state().traffic;
    REQUIRE(table.count() >= 1);
    int alptas = 0;
    for (int i = 0; i < traffic::TrafficTable::kCapacity; i++) {
        const traffic::Target* t = table.at(i);
        if (t != nullptr && t->used && t->obs.source == messages::Source::Alptas) alptas++;
    }
    CHECK(alptas == 1);
}
