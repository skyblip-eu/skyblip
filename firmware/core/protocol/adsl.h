// core/protocol/adsl.h: ADS-L 4 SRD-860 Issue 2 iConspicuity (position) packet.
#ifndef SKYBLIP_CORE_PROTOCOL_ADSL_H
#define SKYBLIP_CORE_PROTOCOL_ADSL_H

#include <cstdint>
#include <cstring>

#include "core/messages/messages.h"

namespace skyblip::protocol {

struct __attribute__((packed)) AdslPacket {
    static constexpr uint8_t kTxBytes = 27;
    static constexpr uint8_t kSync1 = 0x72;
    static constexpr uint8_t kSync2 = 0x4B;
    static constexpr uint8_t kDataBytes = kTxBytes - 3;      // 24: Version+payload+CRC
    static constexpr uint8_t kCrcCoverBytes = kTxBytes - 6;  // 21: Version+payload

    uint8_t SYNC[2];
    uint8_t Length;
    uint8_t Version;  // Version[4]/Signature[1]/Key[2]/Reserved[1]
    union {
        uint32_t Word[5];
        uint8_t Byte[20];
        struct __attribute__((packed)) {
            uint8_t Type;        // 0x02 = iConspicuity; bit7 = unicast
            uint8_t Address[4];  // Address[30]/Reserved[1]/RelayForward[1]
            // Meta[2]: Time[6]/FlightState[2]/Cat[5]/Emergency[3]
            uint8_t TimeStamp : 6;    // [0.25 s]
            uint8_t FlightState : 2;  // 0=unknown,1=ground,2=airborne
            uint8_t AcftCat : 5;      // ADS-L category
            uint8_t Emergency : 3;    // 1 = OK
            uint8_t Position[11];     // Lat[24]/Lon[24]/Speed[8]/Alt[14]/Climb[9]/Track[9]
            // Integrity[2]: SIL[2]/SDA[2]/NIC[4]/NACp[3]/GVA[2]/NACv[2]/Reserved[1]
            uint8_t SourceIntegrity : 2;
            uint8_t DesignAssurance : 2;
            uint8_t NavigIntegrity : 4;
            uint8_t HorizAccuracy : 3;
            uint8_t VertAccuracy : 2;
            uint8_t VelAccuracy : 2;
            uint8_t Reserved : 1;
        };
    };
    uint8_t CRC[3];
    uint8_t SpareByte;

    void init(uint8_t type = 0x02) {
        SYNC[0] = kSync1;
        SYNC[1] = kSync2;
        Length = kTxBytes - 3;
        Version = 0x00;
        for (int i = 0; i < 5; i++) Word[i] = 0;
        Type = type;
        SpareByte = 0;
        CRC[0] = CRC[1] = CRC[2] = 0;
    }

    bool is_position() const { return Type == 0x02; }

    static uint32_t get4(const uint8_t* b) {
        return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
               (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
    }
    static void set4(uint8_t* b, uint32_t w) {
        b[0] = static_cast<uint8_t>(w);
        b[1] = static_cast<uint8_t>(w >> 8);
        b[2] = static_cast<uint8_t>(w >> 16);
        b[3] = static_cast<uint8_t>(w >> 24);
    }
    static uint32_t get3(const uint8_t* b) {
        int32_t w = b[2];
        w <<= 8;
        w |= b[1];
        w <<= 8;
        w |= b[0];
        return w;
    }
    static void set3(uint8_t* b, uint32_t w) {
        b[0] = static_cast<uint8_t>(w);
        b[1] = static_cast<uint8_t>(w >> 8);
        b[2] = static_cast<uint8_t>(w >> 16);
    }

    uint32_t address() const { return (get4(Address) >> 6) & 0x00FFFFFF; }
    void set_address(uint32_t a) {
        uint32_t w = get4(Address);
        w = (w & 0xC000003F) | (a << 6);
        set4(Address, w);
    }
    uint8_t addr_table() const { return Address[0] & 0x3F; }
    void set_addr_table(uint8_t t) { Address[0] = (Address[0] & 0xC0) | t; }
    uint32_t address_and_type() const {
        return (static_cast<uint32_t>(addr_table()) << 24) | address();
    }
    bool is_relay() const { return Address[3] & 0x80; }
    void set_relay(uint8_t r = 1) { Address[3] = (Address[3] & 0x7F) | (r << 7); }

    static int32_t cordic_to_1e7(int32_t c) {
        return (static_cast<int64_t>(c) * 900007296 + (1 << 29)) >> 30;
    }
    static int32_t e7_to_cordic(int32_t c) {
        return (static_cast<int64_t>(c) * 5003959 + (1 << 21)) >> 22;
    }

    int32_t lat_cordic() const {
        int32_t v = get3(Position);
        v <<= 8;
        v >>= 1;
        return v;
    }
    int32_t lon_cordic() const {
        int32_t v = get3(Position + 3);
        v <<= 8;
        return v;
    }
    void set_lat_cordic(int32_t lat) {
        lat = (lat + 0x40) >> 7;
        set3(Position, lat);
    }
    void set_lon_cordic(int32_t lon) {
        lon = (lon + 0x80) >> 8;
        set3(Position + 3, lon);
    }
    int32_t lat_1e7() const { return cordic_to_1e7(lat_cordic()); }
    int32_t lon_1e7() const { return cordic_to_1e7(lon_cordic()); }
    void set_lat_1e7(int32_t v) { set_lat_cordic(e7_to_cordic(v)); }
    void set_lon_1e7(int32_t v) { set_lon_cordic(e7_to_cordic(v)); }

    // Three fields carry an explicit "invalid" code, and the spec is emphatic
    // that it means UNAVAILABLE, not out-of-range: an over-range value "shall be
    // encoded" as the limit (ADS-L 4 SRD860 issue 2, G.1.7 / G.1.8 / G.1.9). The
    // setters below honour that. The set_*_invalid() calls are how a shell says
    // it genuinely does not know.
    static constexpr uint16_t kSpeedInvalidCode = 0xFF;   // G.1.8, 8 bits
    static constexpr uint16_t kClimbInvalidCode = 0x1FF;  // G.1.9, 9 bits
    static constexpr uint16_t kAltInvalidCode = 0x3FFF;   // G.1.7, 14 bits
    static constexpr int32_t kAltOffsetM = 320;           // G.1.7 encoding offset

    uint16_t speed_q() const;      // [0.25 m/s]
    void set_speed_q(uint16_t s);  // [0.25 m/s]
    bool has_speed() const { return Position[6] != kSpeedInvalidCode; }
    void set_speed_invalid() { Position[6] = kSpeedInvalidCode; }

    int32_t alt_m() const;  // [m] HAE (WGS-84 ellipsoid, G.1.7: NOT pressure)
    void set_alt_m(int32_t alt);
    bool alt_invalid() const { return (Position[8] & 0x3F) == 0x3F && Position[7] == 0xFF; }
    void set_alt_invalid() {
        Position[7] = 0xFF;
        Position[8] = static_cast<uint8_t>((Position[8] & 0xC0) | 0x3F);
    }

    int16_t climb_e8() const;  // [0.125 m/s]
    void set_climb_e8(int16_t c);
    bool has_climb() const;
    void set_climb_invalid() { write_climb_code(kClimbInvalidCode); }

    uint16_t track_c9() const;  // 9-bit cordic (512 == 360 deg)
    void set_track_c9(uint16_t w);

    void write_climb_code(uint16_t w);

    void scramble();
    void descramble();
    void set_crc();
    uint32_t check_crc() const;

    int correct(uint8_t* err, int max_bad_bits = 6);
};

static_assert(sizeof(AdslPacket) == 28, "AdslPacket wire size must be 28 bytes");

void to_obs(const AdslPacket& p, uint32_t rx_utc, uint16_t rx_ms, int8_t rssi_dbm,
            messages::Source source, messages::AircraftObs& out);
void from_own(AdslPacket& p, const messages::OwnState& own, uint32_t addr, uint8_t addr_table,
              uint8_t aircraft_cat, bool stealth);

}

#endif
