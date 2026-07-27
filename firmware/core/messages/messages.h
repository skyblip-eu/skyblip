// core/messages/messages.h — "what the system says to itself" (3-ARCHITECTURE
#ifndef SKYBLIP_CORE_MESSAGES_MESSAGES_H
#define SKYBLIP_CORE_MESSAGES_MESSAGES_H

#include <array>
#include <cstdint>

namespace skyblip::messages {

enum class Source : uint8_t { AdslDirect, AdslUplink, Alptas, Own };

struct AircraftObs {
    uint32_t addr;
    uint8_t addr_table;
    uint8_t acft_cat;
    uint8_t flight_state;
    uint8_t emergency;
    int32_t lat_1e7;
    int32_t lon_1e7;
    int32_t alt_m;
    int16_t climb_e8;
    uint16_t speed_q;
    uint16_t track_c9;
    uint32_t rx_utc;
    uint16_t rx_ms;
    int8_t rssi_dbm;
    Source source;
    bool has_climb;
    bool has_speed;
    bool valid_pos;
};

struct OwnState {
    bool fix_valid;
    bool utc_valid;
    bool pps_locked;
    // Vertical rate arrives later than the fix (it needs two samples over a
    // window) and can come from a barometer the unit may not have, so it carries
    // its own validity. ADS-L G.1.9 has an explicit "unavailable" code for it.
    bool climb_valid;
    int32_t lat_1e7;
    int32_t lon_1e7;
    int32_t alt_m;
    int16_t climb_e8;
    uint16_t speed_q;
    uint16_t track_c9;
    uint32_t utc;
    uint8_t sats;
    uint8_t acft_cat;
    uint8_t flight_state;
};

enum class Endpoint : uint8_t { Config, Nmea };

struct LinkUp {
    uint16_t session_id;
    uint16_t mtu;
};
struct LinkDown {
    uint16_t session_id;
};

struct RxFrame {
    uint16_t session_id;
    Endpoint endpoint;
    uint16_t len;
    std::array<uint8_t, 256> data;
};

struct DfuRequest {
    uint16_t session_id;
};

}

#endif
