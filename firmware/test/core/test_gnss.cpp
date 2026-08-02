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
    CHECK(f.alt_m == 545);
    CHECK(int(f.sats) == 8);
    CHECK(int(f.fix_quality) == 1);
}

TEST_CASE("gnss: byte-wise feed reconstructs a sentence") {
    NmeaParser p;
    const char* gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
    bool got = false;
    for (const char* c = gga; *c; c++) got |= p.feed(*c);
    CHECK(got);
    CHECK(p.fix().alt_m == 545);
}

TEST_CASE("gnss: corrupt checksum is rejected, no update") {
    NmeaParser p;
    const char* bad = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00";
    CHECK_FALSE(p.parse_line(bad, static_cast<int>(strlen(bad))));
    CHECK(p.fix().updates == 0);
}
