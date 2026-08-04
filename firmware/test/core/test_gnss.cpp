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
    const char* rmc = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230825,003.1,W*6B";
    CHECK(p.parse_line(rmc, static_cast<int>(strlen(rmc))));
    CHECK(p.last_sentence() == Sentence::Rmc);
    const GnssFix& f = p.fix();
    CHECK(f.valid);
    CHECK(f.utc_valid);
    CHECK(f.lat_1e7 > 480000000);
    CHECK(f.lon_1e7 > 0);
    // 22.4 knots ~= 11.52 m/s -> speed_q ~46
    CHECK(std::abs(int(f.speed_q) - 46) <= 3);
    // track 84.4 deg -> cordic ~120
    CHECK(std::abs(int(f.track_c9) - 120) <= 3);
    // 2025-08-23 12:35:19 UTC epoch
    CHECK(f.utc == 1755952519u);
}

// I, row "Date and jump sanity". An MTK-lineage receiver with no almanac reports
// a date in 1980 beside a position that looks entirely ordinary, and OGN refuses
// it by name (oss/nrf52-ogn-tracker src/ogn.h:737-738). The pinned bug: we took
// any six digits that parsed, so 1980 became an epoch, a flight log session name
// and an ADS-L timestamp, all of them wrong and none of them detectable
// afterwards. The position in the same sentence is not what is being doubted:
// the date is, and it is the date that is dropped.
TEST_CASE("gnss: the MTK year-1980 date is refused, and so is anything before 2000") {
    NmeaParser p;
    const char* lie = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230380,003.1,W*6F";
    REQUIRE(p.parse_line(lie, static_cast<int>(strlen(lie))));
    CHECK(p.fix().valid);  // the receiver still claims a solution
    CHECK_FALSE(p.fix().utc_valid);
    CHECK(p.fix().utc == 0);

    // The boundary: 69 is 2069 and believable, 70 is 1970 and a lie. OGN draws
    // the line in the same place.
    CHECK(kMaxTwoDigitYear == 70);

    // A receiver still counting up from nothing sends all zeroes, which is not a
    // date either: month 0 and day 0 do not exist.
    const char* zeroes = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,000000,003.1,W*65";
    REQUIRE(p.parse_line(zeroes, static_cast<int>(strlen(zeroes))));
    CHECK_FALSE(p.fix().utc_valid);

    // And a good date after a bad one clears it: the flag follows the sentence,
    // it is not sticky.
    const char* good = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230825,003.1,W*6B";
    REQUIRE(p.parse_line(good, static_cast<int>(strlen(good))));
    CHECK(p.fix().utc_valid);
}

// I, row "Leap seconds": DOES NOT APPLY, and this is where it is written down.
// RMC fields 1 and 9 are UTC as the receiver resolved them, so the GPS-UTC
// offset (18 s in 2026) never enters our arithmetic. The moshe-braner fork
// queries it, persists it and reboots when it changes
// (.../src/driver/GNSS.cpp:1610-1679) because it reads u-blox NAV-TIMEGPS, which
// is GPS time. Applying a correction here would move our clock 18 s off UTC.
TEST_CASE("gnss: RMC time is UTC already, so no leap-second offset is applied") {
    NmeaParser p;
    const char* rmc = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230825,003.1,W*6B";
    REQUIRE(p.parse_line(rmc, static_cast<int>(strlen(rmc))));
    // Exactly the UTC epoch of 2025-08-23T12:35:19Z. Not 18 s past it, not 18 s
    // short of it: the same integer any UTC clock would produce.
    CHECK(p.fix().utc == 1755952519u);
    CHECK(p.fix().utc != 1755952519u + 18u);
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

// I, row "Date and jump sanity", second half. A sentence that stopped early
// still carries a checksum over the part that arrived, so the checksum cannot
// catch it: moshe-braner measures the GGA and refuses anything under 40
// characters (.../src/driver/GNSS.cpp:2226-2236). The pinned bug: a GGA cut
// short after the fix quality left the previous altitude standing while the rest
// of the fix moved on, which reads as an aircraft holding altitude perfectly.
TEST_CASE("gnss: a truncated GGA is refused, checksum or no checksum") {
    NmeaParser p;
    const char* whole = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    REQUIRE(p.parse_line(whole, static_cast<int>(strlen(whole))));
    REQUIRE(p.fix().alt_msl_m == 545);

    // Cut after HDOP, re-checksummed: eight fields where ten are needed.
    const char* cut = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9*7C";
    REQUIRE(nmea_checksum_ok(cut, static_cast<int>(strlen(cut))));
    CHECK_FALSE(p.parse_line(cut, static_cast<int>(strlen(cut))));

    // Every field empty: the field count is fine and the length is not, which is
    // the case the length rule exists for.
    const char* empty = "$GPGGA,,,,,,,,,,,,,,*56";
    REQUIRE(nmea_checksum_ok(empty, static_cast<int>(strlen(empty))));
    CHECK(static_cast<int>(strlen(empty)) < kMinGgaLength);
    CHECK_FALSE(p.parse_line(empty, static_cast<int>(strlen(empty))));

    // Neither of them touched the solution we already had.
    CHECK(p.fix().alt_msl_m == 545);
    CHECK(p.fix().updates == 1);
}

// I, row "Receiver identification". $PCAS06 is answered with a $GPTXT banner and
// nothing else on this receiver answers at all, so the banner is the only proof
// the part in front of us speaks $PCAS. SoftRF reads the version out of the same
// offset (oss/SoftRF-lyusupov .../src/driver/GNSS.cpp:981-1010, 1015-1025).
TEST_CASE("gnss: the receiver names itself in a $GPTXT, and the version is kept") {
    NmeaParser p;
    CHECK_FALSE(p.identified());
    CHECK(p.firmware_version()[0] == 0);

    const char* banner = "$GPTXT,01,01,02,SW=URANUS5,V5.1.0.0*1F";
    CHECK(p.parse_line(banner, static_cast<int>(strlen(banner))));
    CHECK(p.last_sentence() == Sentence::Txt);
    CHECK(p.identified());
    CHECK(strcmp(p.firmware_version(), "URANUS5,V5.1.0.0") == 0);

    // A banner is not a solution: the verification window counts fixes, and a
    // receiver introducing itself must not satisfy it.
    CHECK(p.fix().updates == 0);

    // The part emits other $GPTXT lines (antenna status, start-up notices). A
    // version field that is sometimes an antenna warning is worse than none, so
    // only the SW= banner is taken and the version already learned stands.
    const char* antenna = "$GPTXT,01,01,01,ANTSTATUS=OK*38";
    CHECK_FALSE(p.parse_line(antenna, static_cast<int>(strlen(antenna))));
    CHECK(strcmp(p.firmware_version(), "URANUS5,V5.1.0.0") == 0);
}

TEST_CASE("gnss: corrupt checksum is rejected, no update") {
    NmeaParser p;
    const char* bad = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00";
    CHECK_FALSE(p.parse_line(bad, static_cast<int>(strlen(bad))));
    CHECK(p.fix().updates == 0);
}
