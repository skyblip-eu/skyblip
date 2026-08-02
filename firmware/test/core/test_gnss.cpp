// The only door position gets in through. Everything downstream (the alarm, the
// radar, what we transmit about ourselves) is this parser's output, so a sentence
// that fails its checksum must leave the fix untouched rather than half-applied,
// and a sentence arriving one byte at a time must reconstruct exactly.
#include <cstdlib>  // std::abs
#include <cstring>

#include "core/gnss/nmea.h"
#include "doctest/doctest.h"

using namespace skyblip::gnss;

TEST_CASE("gnss: checksum validation") {
    const char* good = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
    CHECK(nmea_checksum_ok(good, static_cast<int>(strlen(good))));
    const char* bad = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*00";
    CHECK_FALSE(nmea_checksum_ok(bad, static_cast<int>(strlen(bad))));
}

TEST_CASE("gnss: coord parse DDMM.mmmm -> 1e-7 deg") {
    int32_t lat = nmea_parse_coord("4807.038", 'N');  // 48 deg 07.038' = 48.1173
    CHECK(std::abs(lat - 481173000) < 20000);
    int32_t lon = nmea_parse_coord("01131.000", 'W');  // -11.51667
    CHECK(lon < 0);
    CHECK(std::abs(lon + 115166667) < 30000);
}

TEST_CASE("gnss: RMC updates fix position, time, speed, track") {
    NmeaParser p;
    const char* rmc = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
    CHECK(p.parse_line(rmc, static_cast<int>(strlen(rmc))));
    const GnssFix& f = p.fix();
    CHECK(f.valid);
    CHECK(f.utc_valid);
    CHECK(f.lat_1e7 > 480000000);
    CHECK(f.lon_1e7 > 0);
    // 22.4 knots ~= 11.52 m/s -> speed_q ~46
    CHECK(std::abs(int(f.speed_q) - 46) <= 3);
    // track 84.4 deg -> cordic ~120
    CHECK(std::abs(int(f.track_c9) - 120) <= 3);
    // 1994-03-23 12:35:19 UTC epoch
    CHECK(f.utc == 764426119u);
}

TEST_CASE("gnss: GGA updates altitude and sats") {
    NmeaParser p;
    const char* gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    CHECK(p.parse_line(gga, static_cast<int>(strlen(gga))));
    const GnssFix& f = p.fix();
    CHECK(f.alt_msl_m == 545);
    CHECK(int(f.sats) == 8);
    CHECK(int(f.fix_quality) == 1);
}

// GGA field 9 is altitude above the geoid (mean sea level); field 11 is the
// separation between the geoid and the WGS-84 ellipsoid at that point. ADS-L 4
// SRD860 issue 2 G.1.7 transmits height above the ELLIPSOID, so the two have to
// be added. Across central Europe the separation is 45-48 m, which is squarely
// inside the vertical alarm window: reporting MSL as if it were HAE puts us that
// far below every correctly implemented neighbour.
TEST_CASE("gnss: GGA carries MSL and ellipsoidal height as separate values") {
    NmeaParser p;
    const char* gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    REQUIRE(p.parse_line(gga, static_cast<int>(strlen(gga))));
    const GnssFix& f = p.fix();
    CHECK(f.alt_msl_valid);
    CHECK(f.alt_msl_m == 545);
    CHECK(f.geoid_separation_m == 47);
    CHECK(f.geoid_separation_measured);
    CHECK(f.alt_hae_valid);
    CHECK(f.alt_m == 592);  // 545.4 + 46.9, the value ADS-L wants
}

// Some receivers omit field 11 entirely and some always answer "0.0,M": OGN
// carries a manual override with a 40 m default (oss/nrf52-ogn-tracker
// src/main.h:38, src/gps.cpp:526-529) and SoftRF special-cases the always-zero
// chipsets (oss/SoftRF-lyusupov .../src/driver/GNSS.cpp:1681-1684). We fall back
// to a named regional constant and flag that the separation was assumed, so a
// wrong altitude is attributable rather than silent.
TEST_CASE("gnss: a receiver that omits the geoid separation falls back and says so") {
    NmeaParser p;
    const char* omitted = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,,M,,*52";
    REQUIRE(p.parse_line(omitted, static_cast<int>(strlen(omitted))));
    CHECK_FALSE(p.fix().geoid_separation_measured);
    CHECK(p.fix().geoid_separation_m == kDefaultGeoidSeparationM);
    CHECK(p.fix().alt_msl_m == 545);
    CHECK(p.fix().alt_m == 545 + kDefaultGeoidSeparationM);
}

TEST_CASE("gnss: a receiver stuck at 0.0 separation falls back too") {
    NmeaParser p;
    const char* zero = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,0.0,M,,*7C";
    REQUIRE(p.parse_line(zero, static_cast<int>(strlen(zero))));
    CHECK_FALSE(p.fix().geoid_separation_measured);
    CHECK(p.fix().geoid_separation_m == kDefaultGeoidSeparationM);
    CHECK(p.fix().alt_m == 545 + kDefaultGeoidSeparationM);
}

// GGA field 8. Without it every integrity and accuracy field we transmit is a
// guess, and a guess encoded as zero reads as "no integrity claimed".
TEST_CASE("gnss: GGA carries HDOP in hundredths") {
    NmeaParser p;
    const char* sharp = "$GPGGA,101530,4736.2417,N,00834.9028,E,1,09,1.25,612.3,M,47.4,M,,*70";
    REQUIRE(p.parse_line(sharp, static_cast<int>(strlen(sharp))));
    CHECK(p.fix().hdop_e2 == 125);

    const char* poor = "$GPGGA,101530,4736.2417,N,00834.9028,E,1,09,4.80,612.3,M,47.4,M,,*7A";
    REQUIRE(p.parse_line(poor, static_cast<int>(strlen(poor))));
    CHECK(p.fix().hdop_e2 == 480);
}

// No fix: no altitude of either kind, and no DOP worth believing.
TEST_CASE("gnss: a GGA without a solution reports no altitude") {
    NmeaParser p;
    const char* none = "$GPGGA,101530,4736.2417,N,00834.9028,E,0,00,99.99,,M,,M,,*7F";
    REQUIRE(p.parse_line(none, static_cast<int>(strlen(none))));
    CHECK_FALSE(p.fix().alt_msl_valid);
    CHECK_FALSE(p.fix().alt_hae_valid);
    CHECK(int(p.fix().fix_quality) == 0);
}

TEST_CASE("gnss: byte-wise feed reconstructs a sentence") {
    NmeaParser p;
    const char* gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
    bool got = false;
    for (const char* c = gga; *c; c++) got |= p.feed(*c);
    CHECK(got);
    CHECK(p.fix().alt_msl_m == 545);
    CHECK(p.fix().alt_m == 592);
}

// A burst is always late relative to the PPS edge whose second it describes, so
// the instant a solution was true is earlier than the instant it arrived. The
// receiver knows by how much and stamps it; this is where it comes off again.
// Neither reference trusts the burst without it: SoftRF subtracts a per-chip
// constant, OGN a PPSdelay parameter defaulting to 100 ms.
TEST_CASE("gnss: a fix is timestamped before its sentence arrived") {
    GnssFix f{};
    f.pps_latency_ms = 135;
    CHECK(fix_instant_ms(f, 10'000) == 9'865);

    // Unstamped, the arrival time is the best we have.
    GnssFix bare{};
    CHECK(fix_instant_ms(bare, 10'000) == 10'000);

    // Boot: the correction reaches back past zero, and the ages computed from it
    // stay right because the arithmetic wraps the same way on both sides.
    CHECK(static_cast<uint32_t>(500 - fix_instant_ms(f, 100)) == 535);
}

TEST_CASE("gnss: corrupt checksum is rejected, no update") {
    NmeaParser p;
    const char* bad = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00";
    CHECK_FALSE(p.parse_line(bad, static_cast<int>(strlen(bad))));
    CHECK(p.fix().updates == 0);
}
