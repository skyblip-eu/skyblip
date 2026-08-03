// L76K GNSS driver tests against models/l76k.h. The driver owns the UART and the
// NMEA parser, so what these pin down is the seam a shell depends on: a fix is
// reported exactly once, a silent receiver never reports one at all (the DFU
// health gate treats that as "cannot talk to its own peripherals"), and the
// receiver is configured rather than left on the factory defaults it boots with.
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "hardware/parts/l76k/l76k.h"
#include "hardware/parts/l76k/model.h"

using namespace skyblip;

namespace {
// One service call and one drain per 10 ms runtime tick, which is how the board
// polls this part.
void run(parts::L76k& driver, models::L76k& chip, uint32_t from_ms, uint32_t to_ms) {
    for (uint32_t t = from_ms; t <= to_ms; t += 10) {
        chip.tick(t);
        driver.service(t);
        driver.poll();
    }
}
}

TEST_CASE("l76k: a new fix is reported exactly once") {
    models::L76k chip;
    chip.fix = true;
    chip.sats = 9;
    chip.alt_m = 1200;
    chip.lat_1e7 = 485000000;
    parts::L76k gnss(chip);

    CHECK_FALSE(gnss.poll());  // the receiver has said nothing yet
    chip.tick(0);              // arms the 1 Hz cadence, emits nothing
    CHECK_FALSE(gnss.poll());

    chip.tick(1000);  // one $GPRMC + $GPGGA burst, >64 B so the chunked read wraps
    CHECK(gnss.poll());
    CHECK(gnss.fix().valid);
    CHECK(gnss.fix().sats == 9);
    CHECK(gnss.fix().alt_m == 1200);
    CHECK(gnss.fix().lat_1e7 == doctest::Approx(485000000).epsilon(0.0001));

    CHECK_FALSE(gnss.poll());  // no new bytes: the same fix is not re-delivered
}

TEST_CASE("l76k: a wired but silent receiver never reports a fix") {
    models::L76k chip;  // never ticked: powered, connected, saying nothing
    parts::L76k gnss(chip);
    for (uint32_t t = 0; t <= 5000; t += 100) CHECK_FALSE(gnss.poll());
    CHECK(gnss.updates() == 0);
}

TEST_CASE("l76k: losing the fix is reported like any other update") {
    models::L76k chip;
    chip.fix = true;
    parts::L76k gnss(chip);
    chip.tick(0);
    chip.tick(1000);
    REQUIRE(gnss.poll());
    REQUIRE(gnss.fix().valid);

    chip.fix = false;
    chip.tick(2000);
    CHECK(gnss.poll());
    CHECK_FALSE(gnss.fix().valid);
}

// The receiver boots with pedestrian smoothing, a factory sentence set and 1 Hz.
// SoftRF sends exactly three commands to this part 250 ms apart
// (oss/SoftRF-lyusupov .../src/driver/GNSS.cpp:1029-1057); the fourth, the fix
// rate, is ours. The model validates the NMEA checksum before applying anything,
// so a driver that miscomputes one configures nothing.
TEST_CASE("l76k: bring-up configures constellations, sentences, dynamic model and rate") {
    models::L76k chip;
    chip.solution_period_ms = models::L76k::kFactoryPeriodMs;
    parts::L76k gnss(chip);

    run(gnss, chip, 0, 1000);

    CHECK(chip.commands_seen == parts::L76k::kCommandCount);
    CHECK(chip.commands_rejected == 0);
    CHECK(chip.constellations == 7);  // GPS + GLONASS + BeiDou
    CHECK(chip.sentence_set_applied);
    CHECK(chip.aviation_dynamic_model());
    CHECK(chip.solution_period_ms == parts::L76k::kFixPeriodMs);
}

// G.1.16 will not transmit a solution older than 500 ms, and the direct slot runs
// to a full second after it. A receiver left at 1 Hz therefore has roughly half
// its solutions refused, which on the bench looks like an intermittent
// transmitter. The configured period has to leave margin under that limit.
TEST_CASE("l76k: the configured fix rate clears the staleness rule") {
    CHECK(parts::L76k::kFixPeriodMs * 2 <= timing::Transmitter::kFixAgeMaxMs);
    CHECK(models::L76k::kFactoryPeriodMs > timing::Transmitter::kFixAgeMaxMs);

    models::L76k chip;
    chip.solution_period_ms = models::L76k::kFactoryPeriodMs;
    parts::L76k gnss(chip);
    run(gnss, chip, 0, 1000);

    uint32_t fixes = 0;
    for (uint32_t t = 1010; t <= 3010; t += 10) {
        chip.tick(t);
        gnss.service(t);
        if (gnss.poll()) fixes++;
    }
    CHECK(fixes >= 2000 / parts::L76k::kFixPeriodMs);
}

// Nothing acknowledges a $PCAS sentence, so the driver treats the receiver's own
// cadence as the acknowledgement. A receiver that hears every command and obeys
// none is a degraded capability, not a working one on silent defaults.
TEST_CASE("l76k: a receiver that never obeys is reported degraded, not assumed good") {
    models::L76k chip;
    chip.accepts_commands = false;
    chip.solution_period_ms = models::L76k::kFactoryPeriodMs;
    parts::L76k gnss(chip);

    run(gnss, chip, 0, 30000);

    CHECK(chip.commands_seen ==
          parts::L76k::kCommandCount * uint32_t(parts::L76k::kMaxConfigAttempts));
    CHECK(gnss.degraded());
    CHECK_FALSE(gnss.configured());
    CHECK(gnss.updates() > 0);  // it is talking, just not listening
}

TEST_CASE("l76k: a receiver that obeys is reported configured") {
    models::L76k chip;
    chip.solution_period_ms = models::L76k::kFactoryPeriodMs;
    parts::L76k gnss(chip);
    CHECK(gnss.config_state() == parts::L76k::Config::Idle);

    run(gnss, chip, 0, 10000);

    CHECK(gnss.configured());
    CHECK_FALSE(gnss.degraded());
}

// The burst is late relative to the PPS edge whose second it describes: SoftRF
// carries 135 ms for RMC on this chip (oss/SoftRF-lyusupov
// .../src/driver/GNSS.cpp:1072-1078) and subtracts it from the arrival time
// (.../src/driver/RF.cpp:236-260). The part stamps the number onto the fix so
// whoever timestamps it can take it off again.
TEST_CASE("l76k: the fix carries the part's burst-to-PPS latency") {
    models::L76k chip;
    parts::L76k gnss(chip);
    chip.tick(0);
    chip.tick(1000);
    REQUIRE(gnss.poll());

    CHECK(gnss.fix().pps_latency_ms == parts::L76k::kPpsLatencyMs);
    CHECK(gnss::fix_instant_ms(gnss.fix(), 1000) == 1000 - parts::L76k::kPpsLatencyMs);
}

// The devicetree pins the UART and nothing in the build compares the two numbers,
// so the driver states the baud it expects. One GGA plus one RMC is about 150
// bytes; at 10 bits a byte that is 156 ms of line time, which has to fit inside
// one solution period with room to spare.
TEST_CASE("l76k: the configured rate fits the baud the devicetree pins") {
    constexpr uint32_t kBurstBytes = 150;
    constexpr uint32_t kBurstMs = kBurstBytes * 10 * 1000 / parts::L76k::kBaudRate;
    CHECK(parts::L76k::kBaudRate == 9600);
    CHECK(kBurstMs < parts::L76k::kFixPeriodMs);
}

// The geometry the circling scenarios rest on, checked against the model that
// produces it rather than assumed. A glider thermalling at 45 kt (23.15 m/s) and
// 13 deg/s flies radius = speed / turn rate = 102 m, a 200 m circle closed in
// 27.7 s, which is the ordinary way a glider climbs and the band
// core/traffic/alarm.h already calls circling. Same figure from the other side:
// radius = speed^2 / (g tan(bank)) puts that circle at 29 deg of bank.
TEST_CASE("l76k: a turn rate flies a circle, and it is the size the arithmetic says") {
    models::L76k chip;
    chip.solution_period_ms = 200;
    chip.speed_kt = 45;
    chip.track_deg = 0;
    chip.turn_dps = 13;
    const int32_t start_lat = chip.lat_1e7;
    const int32_t start_lon = chip.lon_1e7;

    int32_t east_lat = 0, east_lon = 0;
    for (uint32_t t = 0; t <= 27700; t += 100) {
        chip.tick(t);
        // A quarter of the way round a right turn from north, own-ship is due
        // east of where it started by one radius and level with the centre.
        if (t == 6900) {
            east_lat = chip.lat_1e7;
            east_lon = chip.lon_1e7;
        }
    }

    const double north_m = (east_lat - start_lat) * 11132 / 1e6;
    const double east_m = (east_lon - start_lon) * 11132 / 1e6 * 0.6626;
    MESSAGE("quarter circle: " << north_m << " m north, " << east_m << " m east");
    CHECK(east_m == doctest::Approx(102).epsilon(0.05));
    CHECK(north_m == doctest::Approx(102).epsilon(0.05));

    // A full circle later the track is back where it started and so is the
    // aircraft: the integration closes the loop rather than spiralling.
    CHECK((chip.track_deg <= 3 || chip.track_deg >= 357));
    const double closed_m = (chip.lat_1e7 - start_lat) * 11132 / 1e6;
    CHECK(closed_m < 5.0);
    CHECK(closed_m > -5.0);
}
