#include "core/protocol/alptas.h"

#include <cstring>

#include "core/fec/crc.h"
#include "core/protocol/nmea_out.h"

namespace skyblip::protocol {

namespace {

constexpr uint8_t kWords = kAlptasDataBytes / 4;
constexpr uint32_t kDelta = 0x9E3779B9u;
constexpr uint32_t kBteaRounds = 6;
constexpr uint32_t kBteaKey[4] = {0xA5F9B21Cu, 0xAB3F9D12u, 0xC6F34E34u, 0xD72FA378u};
constexpr uint32_t kScrambleKey = 0x956F6C77u;

// INFO: fc 09mar26 field positions are bit offsets from the LSB of byte 0, the
// layout a packed little-endian bit-field struct produces. Spelled out rather
// than declared as bit-fields because the wire order must be identical on the
// host and on Cortex-M4, and packed bit-field allocation is compiler policy, not
// a language guarantee.
struct Field {
    uint16_t lsb;
    uint8_t width;
};
constexpr Field kFAddr{0, 24};
constexpr Field kFMsgType{24, 4};
constexpr Field kFAddrType{28, 3};
constexpr Field kFNeeds3{56, 4};
constexpr Field kFHas3{60, 4};
constexpr Field kFTimeBits{66, 4};
constexpr Field kFAircraftType{70, 4};
constexpr Field kFAlt{75, 13};
constexpr Field kFLat{88, 20};
constexpr Field kFLon{108, 20};
constexpr Field kFTurnRate{128, 9};
constexpr Field kFSpeed{137, 10};
constexpr Field kFVs{147, 9};
constexpr Field kFCourse{156, 10};
constexpr Field kFAirborne{166, 2};
constexpr Field kFGpsA{168, 6};
constexpr Field kFGpsB{174, 5};
constexpr Field kFUnk8{179, 5};
constexpr Field kFLastByte{184, 8};

uint32_t get_field(const uint8_t* data, Field f) {
    uint32_t v = 0;
    for (uint8_t i = 0; i < f.width; i++) {
        uint16_t bit = static_cast<uint16_t>(f.lsb + i);
        if (data[bit >> 3] & (1u << (bit & 7))) v |= 1u << i;
    }
    return v;
}

void put_field(uint8_t* data, Field f, uint32_t v) {
    for (uint8_t i = 0; i < f.width; i++) {
        uint16_t bit = static_cast<uint16_t>(f.lsb + i);
        uint8_t mask = static_cast<uint8_t>(1u << (bit & 7));
        if (v & (1u << i))
            data[bit >> 3] |= mask;
        else
            data[bit >> 3] = static_cast<uint8_t>(data[bit >> 3] & ~mask);
    }
}

uint32_t get_word(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}
void put_word(uint8_t* data, uint32_t w) {
    data[0] = static_cast<uint8_t>(w);
    data[1] = static_cast<uint8_t>(w >> 8);
    data[2] = static_cast<uint8_t>(w >> 16);
    data[3] = static_cast<uint8_t>(w >> 24);
}

uint32_t btea_mix(uint32_t y, uint32_t z, uint32_t sum, uint32_t p, uint32_t e) {
    return (((z >> 5) ^ (y << 2)) + ((y >> 3) ^ (z << 4))) ^
           ((sum ^ y) + (kBteaKey[(p & 3) ^ e] ^ z));
}

void btea4(uint32_t* v, bool encode) {
    constexpr uint32_t n = 4;
    uint32_t y, z, sum, e;
    if (encode) {
        sum = 0;
        z = v[n - 1];
        for (uint32_t r = 0; r < kBteaRounds; r++) {
            sum += kDelta;
            e = (sum >> 2) & 3;
            for (uint32_t p = 0; p < n - 1; p++) {
                y = v[p + 1];
                v[p] += btea_mix(y, z, sum, p, e);
                z = v[p];
            }
            y = v[0];
            v[n - 1] += btea_mix(y, z, sum, n - 1, e);
            z = v[n - 1];
        }
    } else {
        sum = kBteaRounds * kDelta;
        y = v[0];
        for (uint32_t r = 0; r < kBteaRounds; r++) {
            e = (sum >> 2) & 3;
            for (uint32_t p = n - 1; p > 0; p--) {
                z = v[p - 1];
                v[p] -= btea_mix(y, z, sum, p, e);
                y = v[p];
            }
            z = v[n - 1];
            v[0] -= btea_mix(y, z, sum, 0, e);
            y = v[0];
            sum -= kDelta;
        }
    }
}

// INFO: fc 09mar26 second crypto stage: a byte-wide XXTEA-shaped mixer over the
// plaintext first two words, the UTC second and a fixed key, XORed into the last
// four words. It keys on time, which is why a receiver must know the second the
// frame arrived in.
void scramble(uint32_t* data, uint32_t utc) {
    uint8_t keys[16];
    put_word(keys + 0, data[0]);
    put_word(keys + 4, data[1]);
    put_word(keys + 8, utc >> 4);
    put_word(keys + 12, kScrambleKey);

    uint32_t z = keys[15];
    uint32_t sum = 0;
    for (int q = 0; q < 2; q++) {
        sum += kDelta;
        uint32_t y = keys[0];
        for (int p = 0; p < 15; p++) {
            uint32_t x = y;
            y = keys[p + 1];
            x += ((((z >> 5) ^ (y << 2)) + ((y >> 3) ^ (z << 4))) ^ (sum ^ y));
            keys[p] = static_cast<uint8_t>(x);
            z = x & 0xFF;
        }
        uint32_t last = y;
        y = keys[0];
        last += ((((z >> 5) ^ (y << 2)) + ((y >> 3) ^ (z << 4))) ^ (sum ^ y));
        keys[15] = static_cast<uint8_t>(last);
        z = last & 0xFF;
    }

    for (uint8_t i = 0; i < 4; i++) data[2 + i] ^= get_word(keys + 4 * i);
}

void crypt_frame(uint8_t* data, uint32_t utc, bool encode) {
    uint32_t w[kWords];
    for (uint8_t i = 0; i < kWords; i++) w[i] = get_word(data + 4 * i);
    if (encode) {
        scramble(w, utc);
        btea4(w + 2, true);
    } else {
        btea4(w + 2, false);
        scramble(w, utc);
    }
    for (uint8_t i = 0; i < kWords; i++) put_word(data + 4 * i, w[i]);
}

// INFO: fc 09mar26 longitude quantum in 1e-7 deg as a function of |latitude| in
// whole degrees: the protocol widens the step towards the poles so a degree of
// longitude keeps roughly constant ground resolution.
int32_t londiv(int32_t abs_lat_deg) {
    static const uint8_t kNear[65] = {
        53,  53,  54,  54,  55,  55,  56,  56,  57,  57,  58,  58,  59,  59,  60,  60,  61,
        61,  62,  62,  63,  63,  64,  64,  65,  65,  67,  68,  70,  71,  73,  74,  76,  77,
        79,  80,  82,  83,  85,  86,  88,  89,  91,  94,  98,  101, 105, 108, 112, 115, 119,
        122, 126, 129, 137, 144, 152, 159, 167, 174, 190, 205, 221, 236, 252};
    static const uint16_t kPolar[11] = {267, 299, 330, 362, 425, 489, 552, 616, 679, 743, 806};
    if (abs_lat_deg < 14) return 52;
    if (abs_lat_deg < 79) return kNear[abs_lat_deg - 14];
    if (abs_lat_deg > 89) return 806;
    return kPolar[abs_lat_deg - 79];
}

uint32_t enscale(int32_t value, uint32_t mbits, uint32_t ebits, uint32_t sbits) {
    uint32_t offset = 1u << mbits;
    uint32_t signbit = offset << ebits;
    uint32_t negative = 0;
    if (value < 0) {
        if (sbits == 0) return 0;
        value = -value;
        negative = signbit;
    }
    uint32_t magnitude = static_cast<uint32_t>(value);
    if (magnitude < offset) return negative | magnitude;
    uint32_t exponent = 0;
    uint32_t mantissa = offset + magnitude;
    uint32_t mlimit = offset + offset - 1;
    uint32_t elimit = signbit - 1;
    while (mantissa > mlimit) {
        mantissa >>= 1;
        exponent += offset;
        if (exponent > elimit) return negative | elimit;
    }
    mantissa -= offset;
    return negative | exponent | mantissa;
}

int32_t descale(uint32_t value, uint32_t mbits, uint32_t ebits, uint32_t sbits) {
    uint32_t offset = 1u << mbits;
    uint32_t signbit = offset << ebits;
    uint32_t negative = 0;
    if (sbits != 0) negative = value & signbit;
    value &= (signbit - 1);
    if (value >= offset) {
        uint32_t exponent = value >> mbits;
        value &= (offset - 1);
        value += offset;
        value <<= exponent;
        value -= offset;
    }
    int32_t out = static_cast<int32_t>(value);
    return negative ? -out : out;
}

int32_t div_nearest(int32_t value, int32_t divisor) {
    int32_t half = divisor >> 1;
    if (value < 0) return -((-value + half) / divisor);
    return (value + half) / divisor;
}

int32_t unwrap20(uint32_t coded, int32_t reference) {
    int32_t delta = static_cast<int32_t>((coded - static_cast<uint32_t>(reference)) & 0x0FFFFFu);
    if (delta >= 0x080000) delta -= 0x100000;
    return delta + reference;
}

int32_t abs32(int32_t v) { return v < 0 ? -v : v; }

}  // namespace

uint16_t alptas_crc(const uint8_t* data) {
    return fec::crc16_ccitt(data, kAlptasDataBytes, kAlptasCrcInit);
}

void alptas_set_crc(uint8_t* frame) {
    uint16_t crc = alptas_crc(frame);
    frame[kAlptasDataBytes] = static_cast<uint8_t>(crc >> 8);
    frame[kAlptasDataBytes + 1] = static_cast<uint8_t>(crc);
}

bool alptas_crc_ok(const uint8_t* frame) {
    uint16_t carried = static_cast<uint16_t>(frame[kAlptasDataBytes] << 8) |
                       static_cast<uint16_t>(frame[kAlptasDataBytes + 1]);
    return alptas_crc(frame) == carried;
}

uint32_t alptas_address(const uint8_t* frame) { return get_field(frame, kFAddr); }

uint8_t alptas_type_to_adsl_cat(uint8_t alptas_type) {
    static const uint8_t kMap[16] = {0, 4, 1, 3, 8, 1, 7, 7, 1, 2, 3, 5, 5, 11, 0, 0};
    return alptas_type < 16 ? kMap[alptas_type] : 0;
}

uint8_t alptas_addr_type_to_table(uint8_t alptas_addr_type) {
    switch (alptas_addr_type & 3) {
        case 1: return 0x05;
        case 2: return 0x06;
        default: return 0;
    }
}

uint8_t adsl_table_to_alptas_addr_type(uint8_t addr_table) {
    switch (addr_table) {
        case 0x05: return 1;
        case 0x06: return 2;
        default: return 0;
    }
}

Status alptas_decode(const uint8_t* frame, uint32_t rx_utc, int32_t ref_lat_1e7,
                     int32_t ref_lon_1e7, messages::AircraftObs& out) {
    if (!alptas_crc_ok(frame)) return Status::Crc;

    uint8_t data[kAlptasDataBytes];
    __builtin_memcpy(data, frame, kAlptasDataBytes);
    if (get_field(data, kFMsgType) != kAlptasMsgTypePosition) return Status::Unsupported;

    crypt_frame(data, rx_utc, false);

    // INFO: fc 09mar26 the only decrypt check the protocol offers: the frame's 4
    // time bits must agree with the local second to +/-1 (a rollover where one
    // side is 0 fails, upstream accepts that), the last byte must be 0 and the
    // needs3 nibble must be 3. Without this gate a mis-framed foreign packet
    // decrypts into plausible-looking traffic.
    uint32_t timebits = get_field(data, kFTimeBits);
    uint32_t localbits = rx_utc & 0x0Fu;
    bool time_ok =
        (localbits == timebits) || (localbits + 1 == timebits) || (localbits == timebits + 1);
    if (!time_ok) return Status::Invalid;
    if (get_field(data, kFLastByte) != 0 || get_field(data, kFNeeds3) != 3) return Status::Invalid;

    uint32_t course_deg = get_field(data, kFCourse) >> 1;
    if (course_deg > 360) return Status::Invalid;

    int32_t lat_ref = div_nearest(ref_lat_1e7, 52);
    int32_t lat_1e7 = unwrap20(get_field(data, kFLat), lat_ref) * 52;
    int32_t divisor = londiv(abs32(lat_1e7) / 10000000);
    int32_t lon_ref = div_nearest(ref_lon_1e7, divisor);
    int32_t lon_1e7 = unwrap20(get_field(data, kFLon), lon_ref) * divisor;

    int32_t speed_10 = descale(get_field(data, kFSpeed), 8, 2, 0);
    int32_t vs_10 = descale(get_field(data, kFVs), 6, 2, 1);

    out = messages::AircraftObs{};
    out.addr = get_field(data, kFAddr);
    out.addr_table = alptas_addr_type_to_table(static_cast<uint8_t>(get_field(data, kFAddrType)));
    out.aircraft_cat =
        alptas_type_to_adsl_cat(static_cast<uint8_t>(get_field(data, kFAircraftType)));
    out.flight_state = get_field(data, kFAirborne) > 1 ? 2 : 1;
    out.emergency = 1;  // INFO: fc 09mar26 no emergency field on the wire; 1 is ADS-L "OK"
    out.lat_1e7 = lat_1e7;
    out.lon_1e7 = lon_1e7;
    out.alt_m = descale(get_field(data, kFAlt), 12, 1, 0) - 1000;
    out.speed_q = static_cast<uint16_t>(div_nearest(speed_10 * 2, 5));
    out.climb_e8 = static_cast<int16_t>(div_nearest(vs_10 * 4, 5));
    out.track_c9 =
        static_cast<uint16_t>(div_nearest(static_cast<int32_t>(course_deg) * 512, 360) & 0x1FF);
    out.rx_utc = rx_utc;
    out.source = messages::Source::Alptas;
    out.has_speed = true;
    out.has_climb = true;
    out.valid_pos = true;
    return Status::Ok;
}

Status alptas_encode(uint8_t* frame, const messages::AircraftObs& obs, uint32_t utc,
                     int32_t ref_lat_1e7, int32_t ref_lon_1e7) {
    // INFO: fc 09mar26 the reference position is a decode-side input only: the
    // wire carries position modulo 2^20 quanta, absolute, and the receiver
    // reconstructs the high bits from where it is itself (Legacy.cpp:1045).
    (void)ref_lat_1e7;
    (void)ref_lon_1e7;
    if (!obs.valid_pos) return Status::Invalid;

    __builtin_memset(frame, 0, kAlptasFrameBytes);
    put_field(frame, kFAddr, obs.addr & 0x00FFFFFFu);
    put_field(frame, kFMsgType, kAlptasMsgTypePosition);
    put_field(frame, kFAddrType, adsl_table_to_alptas_addr_type(obs.addr_table));
    put_field(frame, kFTimeBits, utc & 0x0Fu);
    put_field(frame, kFAircraftType, adsl_cat_to_alptas(obs.aircraft_cat));
    put_field(frame, kFAirborne, obs.flight_state == 1 ? 1 : 2);
    put_field(frame, kFAlt, enscale(obs.alt_m + 1000, 12, 1, 0));

    int32_t lat_coded = div_nearest(obs.lat_1e7, 52);
    put_field(frame, kFLat, static_cast<uint32_t>(lat_coded) & 0x0FFFFFu);
    int32_t divisor = londiv(abs32(obs.lat_1e7) / 10000000);
    int32_t lon_coded = div_nearest(obs.lon_1e7, divisor);
    put_field(frame, kFLon, static_cast<uint32_t>(lon_coded) & 0x0FFFFFu);

    int32_t speed_10 = obs.has_speed ? div_nearest(obs.speed_q * 5, 2) : 0;
    put_field(frame, kFSpeed, enscale(speed_10, 8, 2, 0));
    int32_t vs_10 = obs.has_climb ? div_nearest(obs.climb_e8 * 5, 4) : 0;
    put_field(frame, kFVs, enscale(vs_10, 6, 2, 1));
    put_field(frame, kFTurnRate, enscale(0, 6, 2, 1));

    uint32_t course_deg = static_cast<uint32_t>(div_nearest(obs.track_c9 * 360, 512)) % 360u;
    put_field(frame, kFCourse, (course_deg << 1) & 0x03FFu);

    // INFO: fc 09mar26 GNSS accuracy and unk8 carry the fixed values upstream
    // transmits (Legacy.cpp:1075-1077): the accuracy fields have a known scaling
    // but no source of truth here, and unk8's meaning is unknown.
    put_field(frame, kFGpsA, 0x12);
    put_field(frame, kFGpsB, 0x0A);
    put_field(frame, kFUnk8, 11);
    put_field(frame, kFNeeds3, 3);
    put_field(frame, kFHas3, 3);

    crypt_frame(frame, utc, true);
    alptas_set_crc(frame);
    return Status::Ok;
}

}
