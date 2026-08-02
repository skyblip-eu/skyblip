#include "core/protocol/adsl.h"

#include "core/fec/crc.h"
#include "core/fec/scramble.h"
#include "core/flight/extrapolate.h"
#include "core/settings/address.h"
#include "core/util/bitcount.h"
#include "core/util/varint.h"

namespace skyblip::protocol {

uint16_t AdslPacket::speed_q() const { return uns_vr_decode<uint16_t, 6>(Position[6]); }
void AdslPacket::set_speed_q(uint16_t s) {
    uint16_t w = uns_vr_encode<uint16_t, 6>(s);
    // uns_vr_encode saturates at 4*thres-1, which for this field IS the invalid
    // code. G.1.8: over-range "shall be encoded" as the maximum (236 m/s =
    // 0xFE), so saturation must stop one short of "unavailable".
    if (w >= kSpeedInvalidCode) w = kSpeedInvalidCode - 1;
    Position[6] = static_cast<uint8_t>(w);
}

int32_t AdslPacket::alt_m() const {
    int32_t w = Position[8] & 0x3F;
    w <<= 8;
    w |= Position[7];
    return uns_vr_decode<int32_t, 12>(w) - 320;
}
void AdslPacket::set_alt_m(int32_t alt) {
    alt += kAltOffsetM;
    if (alt < 0) alt = 0;  // G.1.7: below -320 m encodes the limit, 0x0000
    uint32_t w = uns_vr_encode<uint32_t, 12>(static_cast<uint32_t>(alt));
    // Same collision as ground speed: saturation lands on 0x3FFF, the code for
    // "no 3D fix". G.1.7 wants the limit (0x3FFE, 61104 m) instead - the
    // difference between "very high" and "no idea".
    if (w >= kAltInvalidCode) w = kAltInvalidCode - 1;
    Position[7] = static_cast<uint8_t>(w);
    Position[8] = static_cast<uint8_t>((Position[8] & 0xC0) | ((w >> 8) & 0x3F));
}

int16_t AdslPacket::climb_e8() const {
    int16_t w = Position[9] & 0x7F;
    w <<= 2;
    w |= Position[8] >> 6;
    return sign_vr_decode<int16_t, 6>(w);
}
void AdslPacket::set_climb_e8(int16_t c) {
    uint16_t w = static_cast<uint16_t>(sign_vr_encode<int16_t, 6>(c)) & 0x1FF;
    // A sink rate at or beyond the limit encodes sign|0xFF == 0x1FF, the code for
    // "no vertical rate". G.1.9 wants the limit (-118 m/s), so clamp the
    // magnitude one short. Positive 0x0FF is a legal +119 m/s and stays.
    if (w == kClimbInvalidCode) w = kClimbInvalidCode - 1;
    write_climb_code(w);
}

void AdslPacket::write_climb_code(uint16_t w) {
    Position[8] = static_cast<uint8_t>((Position[8] & 0x3F) | ((w & 0x03) << 6));
    Position[9] = static_cast<uint8_t>((Position[9] & 0x80) | ((w >> 2) & 0x7F));
}
bool AdslPacket::has_climb() const {
    uint16_t w = static_cast<uint16_t>(Position[9] & 0x7F);
    w <<= 2;
    w |= static_cast<uint16_t>(Position[8] >> 6);
    return w != kClimbInvalidCode;
}

uint16_t AdslPacket::track_c9() const {
    int16_t w = Position[10];
    w <<= 1;
    w |= Position[9] >> 7;
    return static_cast<uint16_t>(w & 0x1FF);
}
void AdslPacket::set_track_c9(uint16_t w) {
    Position[9] = (Position[9] & 0x7F) | ((w & 0x01) << 7);
    Position[10] = static_cast<uint8_t>(w >> 1);
}

// XXTEA works on 32-bit words, but this struct is packed, so it has an alignment
// requirement of 1 and an instance can legally sit at an odd address. Handing
// &Word[0] to the scrambler would then be a misaligned uint32_t* - undefined
// behaviour, and on Cortex-M a real fault the moment the optimiser reaches for
// LDM/STM, which require word alignment. It would fault in the RX path, in the
// air, depending on where the packet happened to land on the stack. So copy
// through an aligned local instead: 40 bytes of stack and two 20-byte copies per
// packet, against a few frames per second. GCC catches this as
// -Waddress-of-packed-member. Clang does not, so do not rely on the compiler.
void AdslPacket::scramble() {
    uint32_t w[5];
    __builtin_memcpy(w, Byte, sizeof(w));
    fec::xxtea_scramble_key0(w, 5, 6);
    __builtin_memcpy(Byte, w, sizeof(w));
}
void AdslPacket::descramble() {
    uint32_t w[5];
    __builtin_memcpy(w, Byte, sizeof(w));
    fec::xxtea_descramble_key0(w, 5, 6);
    __builtin_memcpy(Byte, w, sizeof(w));
}

void AdslPacket::set_crc() {
    uint32_t w = fec::adsl_pi_calc(reinterpret_cast<const uint8_t*>(&Version), kCrcCoverBytes);
    CRC[0] = static_cast<uint8_t>(w >> 16);
    CRC[1] = static_cast<uint8_t>(w >> 8);
    CRC[2] = static_cast<uint8_t>(w);
}
uint32_t AdslPacket::check_crc() const {
    return fec::adsl_pi_check(reinterpret_cast<const uint8_t*>(&Version), kDataBytes);
}

namespace {
constexpr uint16_t kBits = AdslPacket::kDataBytes * 8;

void flip_bit(uint8_t* byte, int bit_idx) {
    int byte_idx = bit_idx >> 3;
    bit_idx &= 7;
    bit_idx = 7 - bit_idx;
    byte[byte_idx] ^= static_cast<uint8_t>(1 << bit_idx);
}

uint32_t crc_syndrome(uint8_t bit) {
    static const uint32_t kSyndrome[kBits] = {
        0x7ABEE1, 0xC2A574, 0x6152BA, 0x30A95D, 0xE7AEAA, 0x73D755, 0xC611AE, 0x6308D7, 0xCE7E6F,
        0x98C533, 0xB3989D, 0xA6364A, 0x531B25, 0xD67796, 0x6B3BCB, 0xCA67E1, 0x9AC9F4, 0x4D64FA,
        0x26B27D, 0xECA33A, 0x76519D, 0xC4D2CA, 0x626965, 0xCECEB6, 0x67675B, 0xCC49A9, 0x99DED0,
        0x4CEF68, 0x2677B4, 0x133BDA, 0x099DED, 0xFB34F2, 0x7D9A79, 0xC13738, 0x609B9C, 0x304DCE,
        0x1826E7, 0xF3E977, 0x860EBF, 0xBCFD5B, 0xA184A9, 0xAF3850, 0x579C28, 0x2BCE14, 0x15E70A,
        0x0AF385, 0xFA83C6, 0x7D41E3, 0xC15AF5, 0x9F577E, 0x4FABBF, 0xD82FDB, 0x93EDE9, 0xB60CF0,
        0x5B0678, 0x2D833C, 0x16C19E, 0x0B60CF, 0xFA4A63, 0x82DF35, 0xBE959E, 0x5F4ACF, 0xD05F63,
        0x97D5B5, 0xB410DE, 0x5A086F, 0xD2FE33, 0x96851D, 0xB4B88A, 0x5A5C45, 0xD2D426, 0x696A13,
        0xCB4F0D, 0x9A5D82, 0x4D2EC1, 0xD96D64, 0x6CB6B2, 0x365B59, 0xE4D7A8, 0x726BD4, 0x3935EA,
        0x1C9AF5, 0xF1B77E, 0x78DBBF, 0xC397DB, 0x9E31E9, 0xB0E2F0, 0x587178, 0x2C38BC, 0x161C5E,
        0x0B0E2F, 0xFA7D13, 0x82C48D, 0xBE9842, 0x5F4C21, 0xD05C14, 0x682E0A, 0x341705, 0xE5F186,
        0x72F8C3, 0xC68665, 0x9CB936, 0x4E5C9B, 0xD8D449, 0x939020, 0x49C810, 0x24E408, 0x127204,
        0x093902, 0x049C81, 0xFDB444, 0x7EDA22, 0x3F6D11, 0xE04C8C, 0x702646, 0x381323, 0xE3F395,
        0x8E03CE, 0x4701E7, 0xDC7AF7, 0x91C77F, 0xB719BB, 0xA476D9, 0xADC168, 0x56E0B4, 0x2B705A,
        0x15B82D, 0xF52612, 0x7A9309, 0xC2B380, 0x6159C0, 0x30ACE0, 0x185670, 0x0C2B38, 0x06159C,
        0x030ACE, 0x018567, 0xFF38B7, 0x80665F, 0xBFC92B, 0xA01E91, 0xAFF54C, 0x57FAA6, 0x2BFD53,
        0xEA04AD, 0x8AF852, 0x457C29, 0xDD4410, 0x6EA208, 0x375104, 0x1BA882, 0x0DD441, 0xF91024,
        0x7C8812, 0x3E4409, 0xE0D800, 0x706C00, 0x383600, 0x1C1B00, 0x0E0D80, 0x0706C0, 0x038360,
        0x01C1B0, 0x00E0D8, 0x00706C, 0x003836, 0x001C1B, 0xFFF409, 0x800000, 0x400000, 0x200000,
        0x100000, 0x080000, 0x040000, 0x020000, 0x010000, 0x008000, 0x004000, 0x002000, 0x001000,
        0x000800, 0x000400, 0x000200, 0x000100, 0x000080, 0x000040, 0x000020, 0x000010, 0x000008,
        0x000004, 0x000002, 0x000001};
    return kSyndrome[bit];
}

uint8_t find_crc_syndrome(uint32_t syndr) {
    static const uint32_t kSorted[kBits] = {
        0x000001BF, 0x000002BE, 0x000004BD, 0x000008BC, 0x000010BB, 0x000020BA, 0x000040B9,
        0x000080B8, 0x000100B7, 0x000200B6, 0x000400B5, 0x000800B4, 0x001000B3, 0x001C1BA6,
        0x002000B2, 0x003836A5, 0x004000B1, 0x00706CA4, 0x008000B0, 0x00E0D8A3, 0x010000AF,
        0x01856788, 0x01C1B0A2, 0x020000AE, 0x030ACE87, 0x038360A1, 0x040000AD, 0x049C816D,
        0x06159C86, 0x0706C0A0, 0x080000AC, 0x0939026C, 0x099DED1E, 0x0AF3852D, 0x0B0E2F5A,
        0x0B60CF39, 0x0C2B3885, 0x0DD44197, 0x0E0D809F, 0x100000AB, 0x1272046B, 0x133BDA1D,
        0x15B82D7E, 0x15E70A2C, 0x161C5E59, 0x16C19E38, 0x1826E724, 0x18567084, 0x1BA88296,
        0x1C1B009E, 0x1C9AF551, 0x200000AA, 0x24E4086A, 0x2677B41C, 0x26B27D12, 0x2B705A7D,
        0x2BCE142B, 0x2BFD538F, 0x2C38BC58, 0x2D833C37, 0x304DCE23, 0x30A95D03, 0x30ACE083,
        0x34170561, 0x365B594D, 0x37510495, 0x38132373, 0x3836009D, 0x3935EA50, 0x3E44099A,
        0x3F6D1170, 0x400000A9, 0x457C2992, 0x4701E776, 0x49C81069, 0x4CEF681B, 0x4D2EC14A,
        0x4D64FA11, 0x4E5C9B66, 0x4FABBF32, 0x531B250C, 0x56E0B47C, 0x579C282A, 0x57FAA68E,
        0x58717857, 0x5A086F41, 0x5A5C4545, 0x5B067836, 0x5F4ACF3D, 0x5F4C215E, 0x609B9C22,
        0x6152BA02, 0x6159C082, 0x62696516, 0x6308D707, 0x67675B18, 0x682E0A60, 0x696A1347,
        0x6B3BCB0E, 0x6CB6B24C, 0x6EA20894, 0x70264672, 0x706C009C, 0x726BD44F, 0x72F8C363,
        0x73D75505, 0x76519D14, 0x78DBBF53, 0x7A930980, 0x7ABEE100, 0x7C881299, 0x7D41E32F,
        0x7D9A7920, 0x7EDA226F, 0x800000A8, 0x80665F8A, 0x82C48D5C, 0x82DF353B, 0x860EBF26,
        0x8AF85291, 0x8E03CE75, 0x91C77F78, 0x93902068, 0x93EDE934, 0x96851D43, 0x97D5B53F,
        0x98C53309, 0x99DED01A, 0x9A5D8249, 0x9AC9F410, 0x9CB93665, 0x9E31E955, 0x9F577E31,
        0xA01E918C, 0xA184A928, 0xA476D97A, 0xA6364A0B, 0xADC1687B, 0xAF385029, 0xAFF54C8D,
        0xB0E2F056, 0xB3989D0A, 0xB410DE40, 0xB4B88A44, 0xB60CF035, 0xB719BB79, 0xBCFD5B27,
        0xBE959E3C, 0xBE98425D, 0xBFC92B8B, 0xC1373821, 0xC15AF530, 0xC2A57401, 0xC2B38081,
        0xC397DB54, 0xC4D2CA15, 0xC611AE06, 0xC6866564, 0xCA67E10F, 0xCB4F0D48, 0xCC49A919,
        0xCE7E6F08, 0xCECEB617, 0xD05C145F, 0xD05F633E, 0xD2D42646, 0xD2FE3342, 0xD677960D,
        0xD82FDB33, 0xD8D44967, 0xD96D644B, 0xDC7AF777, 0xDD441093, 0xE04C8C71, 0xE0D8009B,
        0xE3F39574, 0xE4D7A84E, 0xE5F18662, 0xE7AEAA04, 0xEA04AD90, 0xECA33A13, 0xF1B77E52,
        0xF3E97725, 0xF526127F, 0xF9102498, 0xFA4A633A, 0xFA7D135B, 0xFA83C62E, 0xFB34F21F,
        0xFDB4446E, 0xFF38B789, 0xFFF409A7};
    uint16_t bot = 0, top = kBits;
    for (;;) {
        uint16_t mid = (bot + top) >> 1;
        uint32_t mid_syndr = kSorted[mid] >> 8;
        if (syndr == mid_syndr) return static_cast<uint8_t>(kSorted[mid]);
        if (mid == bot) break;
        if (syndr < mid_syndr)
            top = mid;
        else
            bot = mid;
    }
    return 0xFF;
}
}

int AdslPacket::correct(uint8_t* err, int max_bad_bits) {
    uint8_t* data = reinterpret_cast<uint8_t*>(&Version);
    uint32_t crc = fec::adsl_pi_check(data, kDataBytes);
    if (crc == 0) return 0;
    uint8_t single = find_crc_syndrome(crc);
    if (single != 0xFF) {
        flip_bit(data, single);
        return 1;
    }

    static constexpr int kCap = 16;
    if (max_bad_bits > kCap) max_bad_bits = kCap;
    uint8_t idx[kCap];
    uint8_t mask[kCap];
    uint32_t syn[kCap];
    int bad = 0;
    for (int bi = 0; bi < kDataBytes; bi++) {
        uint8_t byte = err[bi];
        uint8_t m = 0x80;
        for (int k = 0; k < 8; k++) {
            if (byte & m) {
                if (bad < max_bad_bits) {
                    idx[bad] = static_cast<uint8_t>(bi);
                    mask[bad] = m;
                    syn[bad] = crc_syndrome(static_cast<uint8_t>(bi * 8 + k));
                }
                bad++;
            }
            m >>= 1;
        }
        if (bad > max_bad_bits) break;
    }
    if (bad > max_bad_bits) return -1;

    int loops = 1 << bad;
    uint8_t prev_gray = 0;
    for (int i = 1; i < loops; i++) {
        uint8_t gray = static_cast<uint8_t>(i ^ (i >> 1));
        uint8_t bit_exp = gray ^ prev_gray;
        int bit = 0;
        while (bit_exp >>= 1) bit++;
        data[idx[bit]] ^= mask[bit];
        crc ^= syn[bit];
        if (crc == 0) return count_ones(gray);
        uint8_t e = find_crc_syndrome(crc);
        if (e != 0xFF) {
            flip_bit(data, e);
            return count_ones(gray) + 1;
        }
        prev_gray = gray;
    }
    return -1;
}

void to_obs(const AdslPacket& p, uint32_t rx_utc, uint16_t rx_ms, int8_t rssi_dbm,
            messages::Source source, messages::AircraftObs& out) {
    out = messages::AircraftObs{};
    out.addr = p.address();
    out.addr_table = p.addr_table();
    out.aircraft_cat = p.AcftCat;
    out.flight_state = p.FlightState;
    out.emergency = p.Emergency;
    out.lat_1e7 = p.lat_1e7();
    out.lon_1e7 = p.lon_1e7();
    out.alt_m = p.alt_m();
    out.has_climb = p.has_climb();
    out.climb_e8 = out.has_climb ? p.climb_e8() : 0;
    out.has_speed = p.has_speed();
    out.speed_q = out.has_speed ? p.speed_q() : 0;
    out.track_c9 = p.track_c9();
    out.rx_utc = rx_utc;
    out.rx_ms = rx_ms;
    out.rssi_dbm = rssi_dbm;
    out.source = source;
    out.valid_pos = true;
}

// ADS-L 4 SRD860 issue 2 G.1.13, NACp. Code 0 is "unknown or HFOM >= 0.5 NM".
uint8_t AdslPacket::horizontal_accuracy_code(uint32_t hfom_cm) {
    static constexpr uint32_t kLimitCm[] = {300, 1000, 3000, 9260, 18520, 55560, 92600};
    for (int i = 0; i < 7; i++)
        if (hfom_cm < kLimitCm[i]) return static_cast<uint8_t>(7 - i);
    return 0;
}

// G.1.14, GVA. 3 = VFOM < 10 m, 2 = < 45 m, 1 = < 150 m, 0 = unknown or worse.
uint8_t AdslPacket::vertical_accuracy_code(uint32_t vfom_cm) {
    if (vfom_cm < 1000) return 3;
    if (vfom_cm < 4500) return 2;
    if (vfom_cm < 15000) return 1;
    return 0;
}

// G.1.15, NACv. A GNSS-only velocity is as good as the position fix behind it,
// which is the relation the reference encoder uses verbatim
// (oss/SoftRF-moshe-braner .../libraries/OGN/ads-l.h:453).
uint8_t AdslPacket::velocity_accuracy_code(uint8_t horizontal_code) {
    return horizontal_code >= 4 ? static_cast<uint8_t>(horizontal_code - 4) : 0;
}

// G.1.12, NIC: the containment radius Rc the position is claimed to lie within.
// 12 = Rc < 7.5 m, 11 = < 25 m, 10 = < 75 m, 9 = < 0.1 NM, down to 1.
uint8_t AdslPacket::navigation_integrity_code(uint32_t containment_cm) {
    static constexpr uint32_t kLimitCm[] = {750,    2500,   7500,   18520,   37040,  111120,
                                            185200, 370400, 740800, 1481600, 3703000};
    for (int i = 0; i < 11; i++)
        if (containment_cm < kLimitCm[i]) return static_cast<uint8_t>(12 - i);
    return 1;
}

void AdslPacket::set_integrity_unknown() {
    SourceIntegrity = 0;
    DesignAssurance = 0;
    NavigIntegrity = 0;
    HorizAccuracy = 0;
    VertAccuracy = 0;
    VelAccuracy = 0;
}

void AdslPacket::set_integrity_from_hdop_e2(uint16_t hdop_e2) {
    if (hdop_e2 == 0) {
        set_integrity_unknown();
        return;
    }
    // We only ask the receiver for GGA and RMC, so there is no VDOP to read and
    // HDOP stands in for it. The vertical figure is the larger of the two per
    // unit of DOP, so the substitution does not flatter the claim.
    const uint32_t hfom_cm = hdop_e2 * kHorizontalErrorPerDopCm / 100;
    const uint32_t vfom_cm = hdop_e2 * kVerticalErrorPerDopCm / 100;

    // No RAIM and no protection level from this receiver, so the containment
    // radius we claim is the accuracy itself, and SourceIntegrity says how much
    // that claim is worth: 1e-3 per flight hour, the honest figure for an
    // unaugmented, unmonitored GNSS. DesignAssurance stays 0 because this
    // firmware carries no design assurance credit.
    SourceIntegrity = kSourceIntegrity1e3;
    DesignAssurance = kDesignAssuranceNone;
    NavigIntegrity = navigation_integrity_code(hfom_cm);
    HorizAccuracy = horizontal_accuracy_code(hfom_cm);
    VertAccuracy = vertical_accuracy_code(vfom_cm);
    VelAccuracy = velocity_accuracy_code(HorizAccuracy);
}

// Stealth, in a protocol that has no stealth bit. FLARM's wire format carries
// one and SoftRF sets it while zeroing the vertical rate
// (oss/SoftRF-lyusupov .../src/protocol/radio/Legacy.cpp:300, 308-309). ADS-L
// handles privacy through the address table instead - tables 0 to 4 are
// self-minted, unregistered identities - and through the G.1.9 code that says
// the vertical rate is unavailable rather than zero. Zero would claim level
// flight, which is the one thing a pilot who asked for stealth is hiding. What
// this does not buy is anonymity over time: the address itself is still
// constant, and rotating it is a separate piece of work.
constexpr uint8_t kAnonymousAddrTable = 0;

uint8_t timestamp_code(uint32_t utc, int32_t lead_ms) {
    constexpr int64_t kCycleMs = static_cast<int64_t>(kTimeStampCycleS) * 1000;
    int64_t ms = static_cast<int64_t>(utc % kTimeStampCycleS) * 1000 + lead_ms;
    ms %= kCycleMs;
    if (ms < 0) ms += kCycleMs;
    return static_cast<uint8_t>(ms / kTimeStampQuarterMs);
}

void from_own(AdslPacket& p, const messages::OwnState& own, uint32_t addr, uint8_t addr_table,
              uint8_t aircraft_cat, bool stealth) {
    from_own(p, own, addr, addr_table, aircraft_cat, stealth, BurstInstant{own.utc, 0, 0});
}

void from_own(AdslPacket& p, const messages::OwnState& own, uint32_t addr, uint8_t addr_table,
              uint8_t aircraft_cat, bool stealth, const BurstInstant& at) {
    p.init(0x02);
    const uint8_t table = stealth ? kAnonymousAddrTable : addr_table;
    p.set_address(settings::safe_air_address(addr, table));
    p.set_addr_table(table);

    // The burst leaves later than the fix was solved, so the position goes
    // forward to the instant the timestamp names. Past the model's bound the
    // fix goes out as it stands, dated when it was solved: neither half of the
    // pair is allowed to describe an instant the other does not.
    const flight::Prediction where = flight::extrapolate(own, at.since_fix_ms);
    p.TimeStamp =
        timestamp_code(at.utc, where.valid ? at.into_utc_ms : at.into_utc_ms - at.since_fix_ms);
    p.FlightState = own.flight_state;
    p.AcftCat = aircraft_cat;
    p.Emergency = 1;
    p.set_lat_1e7(where.lat_1e7);
    p.set_lon_1e7(where.lon_1e7);

    // Altitude and ground speed come from the 3D fix, so without one they are
    // UNAVAILABLE and must say so (G.1.7, G.1.8). Transmitting a stale or zeroed
    // altitude as if it were valid is worse than transmitting nothing: a receiver
    // would compute relative vertical separation against it.
    if (own.fix_valid) {
        p.set_alt_m(where.alt_m);
        p.set_speed_q(own.speed_q);
        p.set_integrity_from_hdop_e2(own.hdop_e2);
    } else {
        p.set_alt_invalid();
        p.set_speed_invalid();
        p.set_integrity_unknown();
    }

    // Vertical rate needs two samples over a window, so it arrives later than the
    // fix and has its own validity. Encoding 0 before then would claim level
    // flight (G.1.9).
    if (own.climb_valid && !stealth)
        p.set_climb_e8(own.climb_e8);
    else
        p.set_climb_invalid();

    p.set_track_c9(where.track_c9);
}

}
