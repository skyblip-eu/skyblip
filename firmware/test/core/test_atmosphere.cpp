// Standard-atmosphere tests. The oracle is the ICAO formula itself, evaluated
// independently here in floating point: the shipping code is an integer table,
// so agreeing with the closed form is a real check, not a restatement.
#include <cmath>

#include "core/flight/atmosphere.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {

// h = 44330.77 * (1 - (p/101325)^0.190263), in centimetres.
double isa_alt_cm(double pa) {
    return 44330.77 * (1.0 - std::pow(pa / 101325.0, 0.190263)) * 100.0;
}

}  // namespace

TEST_CASE("atmosphere: the table agrees with the ICAO closed form") {
    // 25 cm is the worst linear-interpolation error between table entries.
    for (uint32_t pa = 26000; pa <= 110000; pa += 137) {  // 137: a prime, so it lands off-grid
        const int32_t got = flight::pressure_to_alt_cm(pa);
        CHECK(std::abs(got - isa_alt_cm(pa)) < 25.0);
    }
}

TEST_CASE("atmosphere: sea level is zero and the curve descends with pressure") {
    // Approx(0) would be a relative test against zero. 25 cm is the table's bound.
    CHECK(std::abs(flight::pressure_to_alt_cm(flight::kIsaSeaLevelPa)) < 25);

    int32_t prev = flight::pressure_to_alt_cm(26000);
    for (uint32_t pa = 26500; pa <= 110000; pa += 500) {
        const int32_t alt = flight::pressure_to_alt_cm(pa);
        CHECK(alt < prev);  // strictly monotonic: no plateau to hide a bad entry
        prev = alt;
    }
}

TEST_CASE("atmosphere: out-of-range pressure clamps instead of wrapping") {
    CHECK(flight::pressure_to_alt_cm(0) == flight::pressure_to_alt_cm(26000));
    CHECK(flight::pressure_to_alt_cm(1) == flight::pressure_to_alt_cm(20000));
    CHECK(flight::pressure_to_alt_cm(500000) == flight::pressure_to_alt_cm(110000));
}

TEST_CASE("atmosphere: the inverse round-trips through the forward curve") {
    for (int32_t alt_cm = -50000; alt_cm <= 900000; alt_cm += 4321) {
        const uint32_t pa = flight::alt_cm_to_pressure(alt_cm);
        // Bisection lands within one pascal, which is well under a metre.
        CHECK(std::abs(flight::pressure_to_alt_cm(pa) - alt_cm) < 100);
    }
}

TEST_CASE("atmosphere: climb rate in eighth-m/s, both signs") {
    int16_t e8 = 0;
    // +10 m over 2 s = +5 m/s = 40 eighths.
    REQUIRE(flight::climb_e8_from_alt(101000, 100000, 2000, e8));
    CHECK(e8 == 40);
    // Sinking at the same rate.
    REQUIRE(flight::climb_e8_from_alt(100000, 101000, 2000, e8));
    CHECK(e8 == -40);
    // Level flight.
    REQUIRE(flight::climb_e8_from_alt(100000, 100000, 2000, e8));
    CHECK(e8 == 0);
}

TEST_CASE("atmosphere: an unusable window is refused, not guessed at") {
    int16_t e8 = 123;
    CHECK_FALSE(flight::climb_e8_from_alt(101000, 100000, 0, e8));
    CHECK_FALSE(flight::climb_e8_from_alt(101000, 100000, 100, e8));
    CHECK_FALSE(flight::climb_e8_from_alt(101000, 100000, 60000, e8));
    CHECK(e8 == 123);  // the caller's value is left alone
}

TEST_CASE("atmosphere: an absurd rate saturates instead of overflowing int16") {
    int16_t e8 = 0;
    REQUIRE(flight::climb_e8_from_alt(900000, -50000, 500, e8));
    CHECK(e8 == 32767);
    REQUIRE(flight::climb_e8_from_alt(-50000, 900000, 500, e8));
    CHECK(e8 == -32768);
}

TEST_CASE("atmosphere: a real climb through the table reads back as its rate") {
    // 1000 m to 1010 m in 2 s is +5 m/s, computed only from pressures.
    const uint32_t p0 = flight::alt_cm_to_pressure(100000);
    const uint32_t p1 = flight::alt_cm_to_pressure(101000);
    int16_t e8 = 0;
    REQUIRE(flight::climb_e8_from_alt(flight::pressure_to_alt_cm(p1),
                                      flight::pressure_to_alt_cm(p0), 2000, e8));
    CHECK(e8 == doctest::Approx(40).epsilon(0.05));
}
