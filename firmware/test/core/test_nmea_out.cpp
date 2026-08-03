// What an EFB is told, in the FLARM sentences it already speaks. The geometry is
// the part with teeth: PFLAA is relative to own-ship, so a sign error puts traffic
// on the wrong side of the aircraft on a moving map. Without an own position there
// is no relative anything, and the encoder must emit nothing rather than a target
// relative to (0, 0).
#include <cstdlib>  // std::abs
#include <cstring>
#include <string>

#include "core/comms/config.h"
#include "core/protocol/nmea_out.h"
#include "doctest/doctest.h"
#include "hal/link.h"

using namespace skyblip;
using namespace skyblip::protocol;

static bool checksum_ok(const std::string& s) {
    auto star = s.find('*');
    if (star == std::string::npos) return false;
    uint8_t cs = 0;
    for (size_t i = 1; i < star; i++) cs ^= static_cast<uint8_t>(s[i]);
    char hh[3] = {s[star + 1], s[star + 2], 0};
    return static_cast<uint8_t>(std::stoi(hh, nullptr, 16)) == cs;
}

static messages::OwnState own_at(int32_t lat, int32_t lon, int32_t alt) {
    messages::OwnState o{};
    o.fix_valid = true;
    o.utc_valid = true;
    o.lat_1e7 = lat;
    o.lon_1e7 = lon;
    o.alt_m = alt;
    return o;
}

TEST_CASE("nmea: checksum appended correctly") {
    char buf[128];
    int n = format_pgrmz(buf, sizeof(buf), 3500);
    std::string s(buf, n);
    CHECK(s.rfind("$PGRMZ,", 0) == 0);
    CHECK(checksum_ok(s));
    CHECK(s.substr(s.size() - 2) == "\r\n");
}

TEST_CASE("nmea: relative geometry, target due north is +north, ~0 east") {
    auto own = own_at(481000000, 81000000, 1000);
    messages::AircraftObs t{};
    t.valid_pos = true;
    t.lat_1e7 = own.lat_1e7 + 10000000;  // +1 deg lat ~ 111 km north
    t.lon_1e7 = own.lon_1e7;
    t.alt_m = 1200;
    int32_t n_m, e_m, u_m;
    REQUIRE(relative_ned(own, t, n_m, e_m, u_m));
    CHECK(n_m > 100000);
    CHECK(std::abs(e_m) < 100);
    CHECK(u_m == 200);
}

TEST_CASE("nmea: PFLAA carries id, relative pos, checksum") {
    auto own = own_at(481000000, 81000000, 1000);
    messages::AircraftObs t{};
    t.valid_pos = true;
    t.addr = 0xC5D804;
    t.addr_table = 6;  // FLARM -> IDType 2
    t.aircraft_cat = 4;
    t.has_speed = true;
    t.speed_q = 120;
    t.has_climb = true;
    t.climb_e8 = 16;
    t.track_c9 = 128;
    t.lat_1e7 = own.lat_1e7 + 100000;  // ~1.1 km north
    t.lon_1e7 = own.lon_1e7 + 100000;
    t.alt_m = 1150;
    char buf[128];
    int n = format_pflaa(buf, sizeof(buf), own, t, 2);
    REQUIRE(n > 0);
    std::string s(buf, n);
    CHECK(s.rfind("$PFLAA,2,", 0) == 0);
    CHECK(s.find(",2,C5D804,") != std::string::npos);  // IDType 2, hex id
    CHECK(checksum_ok(s));
}

TEST_CASE("nmea: PFLAA returns 0 without own position") {
    messages::OwnState own{};  // no fix
    messages::AircraftObs t{};
    t.valid_pos = true;
    char buf[128];
    CHECK(format_pflaa(buf, sizeof(buf), own, t, 0) == 0);
}

TEST_CASE("nmea: PFLAU reports rx count, gps and threat") {
    auto own = own_at(481000000, 81000000, 1000);
    messages::AircraftObs threat{};
    threat.addr = 0x112233;
    char buf[128];
    int n = format_pflau(buf, sizeof(buf), own, 5, &threat, 3, 45, -50, 800);
    std::string s(buf, n);
    CHECK(s.rfind("$PFLAU,5,1,2,1,3,", 0) == 0);
    CHECK(s.find("112233") != std::string::npos);
    // A threat off the left wing is a negative bearing, not 315.
    const int back = format_pflau(buf, sizeof(buf), own, 5, &threat, 3, -45, -50, 800);
    CHECK(std::string(buf, back).find(",-45,") != std::string::npos);
    CHECK(checksum_ok(s));
}

TEST_CASE("nmea: category mapping ADS-L glider -> ALP-TAS 1") {
    CHECK(adsl_cat_to_alptas(4) == 0x1);  // glider
    CHECK(adsl_cat_to_alptas(3) == 0x3);  // heli
    CHECK(addr_table_to_idtype(5) == 1);  // ICAO
    CHECK(addr_table_to_idtype(6) == 2);  // FLARM
    CHECK(addr_table_to_idtype(0) == 0);  // random
}

TEST_CASE("nmea: the widest sentence these can produce still fits the narrowest payload") {
    // There is no sender on this endpoint yet. When there is, it goes through
    // hal::Link::send like the other two, which refuses a frame longer than the
    // negotiated payload - so what these can produce at their widest is a budget
    // worth pinning now rather than discovering on someone's iPhone.
    messages::OwnState own = own_at(-899999999, -1799999999, -999);
    own.utc_valid = true;
    messages::AircraftObs t{};
    t.valid_pos = true;
    t.addr = 0xFFFFFF;
    t.addr_table = 0x06;
    t.lat_1e7 = 899999999;
    t.lon_1e7 = 1799999999;
    t.alt_m = 99999;
    t.track_c9 = 511;
    t.speed_q = 65535;
    t.climb_e8 = 32767;
    t.has_speed = true;
    t.has_climb = true;
    t.aircraft_cat = 11;
    t.flight_state = 2;

    char buf[256];
    const int traffic = format_pflaa(buf, sizeof(buf), own, t, 3);
    CHECK(traffic > 0);
    CHECK(traffic <= comms::kSmallestSupportedPayload);
    const int status = format_pflau(buf, sizeof(buf), own, 99, &t, 3, -180, -99999, 999999);
    CHECK(status > 0);
    CHECK(status <= comms::kSmallestSupportedPayload);
    // And neither fits what BLE merely guarantees, which is why a sender here
    // cannot assume a frame is a sentence.
    CHECK(traffic > hal::kMinimumLinkPayload);
}
