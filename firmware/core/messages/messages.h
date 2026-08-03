#ifndef SKYBLIP_CORE_MESSAGES_MESSAGES_H
#define SKYBLIP_CORE_MESSAGES_MESSAGES_H

#include <array>
#include <cstdint>

namespace skyblip::messages {

enum class Source : uint8_t { AdslDirect, AdslUplink, Alptas, Own };

struct AircraftObs {
    uint32_t addr;
    uint8_t addr_table;
    uint8_t aircraft_cat;
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
    // Height above the WGS-84 ellipsoid: ADS-L G.1.7 transmits HAE, and every
    // neighbour altitude we receive is measured against the same datum, so a
    // relative height only means something if ours uses it too. alt_msl_m is the
    // geoid-referenced figure a panel or an IGC file wants.
    int32_t alt_m;
    int32_t alt_msl_m;
    int16_t climb_e8;
    uint16_t speed_q;
    uint16_t track_c9;
    // Rate of turn, degrees per second, positive to the right. The extrapolation
    // to the transmit instant needs it, so it is own-ship state and not a
    // display value.
    int16_t turn_dps;
    uint32_t utc;
    // When the fix that made this state arrived. ADS-L G.1.16 refuses to
    // transmit a navigation solution older than 500 ms.
    uint32_t fix_ms;
    // Horizontal dilution of precision in hundredths. Zero means the receiver
    // did not report it, which is not the same as a good one.
    uint16_t hdop_e2;
    // False when the geoid separation behind alt_m came from a regional constant
    // rather than from the receiver.
    bool geoid_separation_measured;
    // How far the constant-speed, constant-turn, constant-climb model missed the
    // fix that has just arrived, in metres. A model that is wrong is worse than
    // no model, and this is how that is found on the bench.
    uint16_t pred_resid_m;
    bool pred_resid_valid;
    // The receiver has held a fix long enough for the solution behind it to have
    // settled. Nothing is transmitted before this is true.
    bool tx_settled;
    // True for the one pass that applied the receiver's first fix since boot.
    // Own-ship is the only writer, so whoever annunciates it reads it here
    // instead of keeping a second watch on the same fact.
    bool fix_acquired;
    uint8_t sats;
    uint8_t aircraft_cat;
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

enum class RfEventType : uint8_t { RxDone, CrcError, TxDone, Missed, TxBusy };

struct RfEvent {
    RfEventType type;
    uint8_t len;
    int8_t rssi_dbm;
    uint64_t at_us;
    std::array<uint8_t, 64> data;
};

struct BaroSample {
    uint32_t pressure_pa;
    uint32_t at_ms;
};

struct ButtonEvent {
    uint8_t id;
};

// The cell's terminal voltage, and whether something is feeding the charger.
// What that pair means is core/power's problem, not the board's.
struct BatterySample {
    uint16_t millivolts;
    bool external_power;
};

}

#endif
