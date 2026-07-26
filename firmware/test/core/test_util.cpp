#include <cstdlib>  // std::abs
#include <cstring>

#include "core/util/fifo.h"
#include "core/util/format.h"
#include "core/util/intmath.h"
#include "core/util/result.h"
#include "core/util/units.h"
#include "core/util/varint.h"
#include "doctest/doctest.h"

using namespace skyblip;

TEST_CASE("units: feet<->metres round-trip is close") {
    for (int ft = -1000; ft <= 40000; ft += 137) {
        Metres m = to_metres(Feet(ft));
        Feet back = to_feet(m);
        // integer double-conversion truncation; a few feet is physically moot
        CHECK(std::abs(back.v - ft) <= 5 + std::abs(ft) / 6000);
    }
}

TEST_CASE("units: cordic9 to degrees") {
    CHECK(to_degrees(Cordic9(0)).v == 0);
    CHECK(to_degrees(Cordic9(128)).v == 90);
    CHECK(to_degrees(Cordic9(256)).v == 180);
    CHECK(to_degrees(Cordic9(384)).v == 270);
}

TEST_CASE("result: ok and error carry status") {
    Result<int> good(42);
    CHECK(good.ok());
    CHECK(good.value() == 42);
    Result<int> bad(Status::Invalid);
    CHECK_FALSE(bad.ok());
    CHECK(bad.status() == Status::Invalid);
    CHECK(bad.value_or(-1) == -1);
}

TEST_CASE("fifo: bounded ring behaviour") {
    Fifo<int, 4> q;  // capacity 3
    CHECK(q.empty());
    CHECK(q.push(1) == Status::Ok);
    CHECK(q.push(2) == Status::Ok);
    CHECK(q.push(3) == Status::Ok);
    CHECK(q.full());
    CHECK(q.push(4) == Status::Full);
    CHECK(q.pop().value() == 1);
    CHECK(q.pop().value() == 2);
    CHECK(q.push(5) == Status::Ok);
    CHECK(q.pop().value() == 3);
    CHECK(q.pop().value() == 5);
    CHECK(q.pop().status() == Status::Empty);
}

TEST_CASE("varint: UnsVR encode/decode round-trip (within representable range)") {
    // Bits=6 range encoding is representable up to ~956; below that the coarsest
    // quantization step is 8.
    for (uint16_t v = 0; v < 900; v += 7) {
        uint16_t enc = uns_vr_encode<uint16_t, 6>(v);
        uint16_t dec = uns_vr_decode<uint16_t, 6>(enc);
        CHECK(std::abs(int(dec) - int(v)) <= 8);  // within the coarsest step
    }
    // Saturation: large values clamp to the max code, not garbage.
    CHECK(uns_vr_decode<uint16_t, 6>(uns_vr_encode<uint16_t, 6>(5000)) <= 960);
}

TEST_CASE("varint: SignVR preserves sign and magnitude bucket") {
    for (int16_t v = -900; v < 900; v += 13) {
        int16_t enc = sign_vr_encode<int16_t, 6>(v);
        int16_t dec = sign_vr_decode<int16_t, 6>(enc);
        bool sign_ok = ((v < 0) == (dec < 0)) || (dec == 0);
        CHECK(sign_ok);
        CHECK(std::abs(std::abs(v) - std::abs(dec)) <= 8);
    }
}

TEST_CASE("varint: UR2V8 battery codec round-trip") {
    for (uint16_t v = 0; v < 3832; v += 11) {
        uint16_t dec = decode_ur2v8(encode_ur2v8(v));
        CHECK(std::abs(int(dec) - int(v)) <= 8);
    }
}

TEST_CASE("intmath: isin/icos magnitude and quadrature") {
    CHECK(std::abs(isin(0)) <= 1);
    CHECK(std::abs(isin(0x4000) - 0x4000) <= 2);  // sin(90) == 1.0
    CHECK(std::abs(icos(0) - 0x4000) <= 2);       // cos(0) == 1.0
    CHECK(std::abs(isin(int16_t(0x8000))) <= 2);  // sin(180) == 0
    // sin^2 + cos^2 ~= 1 (scaled to 0x4000^2)
    for (int a = 0; a < 0x10000; a += 997) {
        int32_t s = isin(int16_t(a));
        int32_t c = icos(int16_t(a));
        int32_t mag = (s * s + c * c) >> 14;  // ~0x4000
        CHECK(std::abs(mag - 0x4000) <= 40);
    }
}

TEST_CASE("intmath: isqrt and distance") {
    CHECK(isqrt<uint32_t>(0) == 0);
    CHECK(isqrt<uint32_t>(144) == 12);
    CHECK(isqrt<uint32_t>(1000000) == 1000);
    CHECK(idistance(3000, 4000) == 5000);
    // fast distance within a few percent of exact
    uint32_t exact = idistance(3000, 4000);
    int32_t fast = ifast_distance(3000, 4000);
    CHECK(std::abs(int(fast) - int(exact)) < int(exact) / 20);
}

TEST_CASE("intmath: iatan2 cardinal directions (16-bit cyclic)") {
    auto deg = [](int16_t a) { return (int(uint16_t(a)) * 360) / 65536; };
    CHECK(deg(iatan2(0, 1000)) == 0);
    CHECK(std::abs(deg(iatan2(1000, 0)) - 90) <= 1);
    CHECK(std::abs(deg(iatan2(0, -1000)) - 180) <= 1);
}

TEST_CASE("format: hex, dec, fixed point") {
    char b[32];
    int n = fmt_hex(b, 0xABCD, 4);
    CHECK(std::string(b, n) == "ABCD");
    n = fmt_uint(b, 42);
    CHECK(std::string(b, n) == "42");
    n = fmt_uint(b, 5, 3);
    CHECK(std::string(b, n) == "005");
    n = fmt_int(b, -17);
    CHECK(std::string(b, n) == "-17");
    n = fmt_uint(b, 1234, 1, 1);  // one decimal
    CHECK(std::string(b, n) == "123.4");
}

TEST_CASE("format: nmea lat/lon") {
    char b[32];
    int n = fmt_nmea_lat(b, 481234500);  // 48.12345 deg
    std::string s(b, n);
    CHECK(s.substr(0, 2) == "48");
    CHECK(s.back() == 'N');
    n = fmt_nmea_lon(b, -81234500);
    s.assign(b, n);
    CHECK(s.substr(0, 3) == "008");
    CHECK(s.back() == 'W');
}
