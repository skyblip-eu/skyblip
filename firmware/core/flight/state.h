// core/flight/state.h: airborne or on the ground, decided from the fix stream
// alone. It is not a display value: it gates the DFU lockout and the transmit
// rate, so a state that flips on a windy ridge start costs twice - it unlocks
// the firmware update in flight and it drops the position rate to the one a
// parked device uses.
//
// A ground speed threshold cannot answer this. A glider on a ridge with the
// wind on the nose has almost no ground speed and is very much flying; a glider
// being towed to the launch point has ground speed and is not. What separates
// them is the vertical rate and how long the picture holds.
#ifndef SKYBLIP_CORE_FLIGHT_STATE_H
#define SKYBLIP_CORE_FLIGHT_STATE_H

#include <cstdint>

namespace skyblip::flight {

// ADS-L 4 SRD860 issue 2 G.1.4 codes: the wire values, so nothing has to map.
enum class FlightState : uint8_t { Unknown = 0, OnGround = 1, Airborne = 2 };

// One GNSS solution, in the units own-ship already carries it in.
struct FlightSample {
    uint32_t at_ms{0};
    uint16_t speed_q{0};   // quarter metres per second
    int16_t climb_e8{0};   // eighth metres per second
    int32_t alt_msl_m{0};  // above mean sea level, which is what the rule means
    uint16_t hdop_e2{0};   // hundredths; zero means the receiver did not report
    bool fix_valid{false};
    bool climb_valid{false};
};

// INFO: fl 02aug26 The criterion is OGN's, in eighth-metres per second so the
// climb term keeps its resolution: horizontal speed plus four times the
// absolute vertical speed, derated by DOP, over 4.0 m/s held for 5 s to declare
// a takeoff and half of that lost for 10 s to declare a landing
// (oss/nrf52-ogn-tracker src/flight.h:16-20, 66-120). The asymmetry is the
// point: taking off wrongly costs a locked-out update, landing wrongly costs a
// transmitter that goes quiet in the air, so the way out of "airborne" is twice
// as slow and half as easily satisfied.
constexpr int32_t kTakeoffMotionE8 = 32;
constexpr int32_t kLandingMotionE8 = 16;
constexpr int32_t kClimbWeight = 4;
constexpr uint32_t kTakeoffHoldMs = 5000;
constexpr uint32_t kLandingHoldMs = 10000;

// Above this there is no ground to be on: OGN takes the altitude alone as proof
// of flight before it looks at any velocity at all.
constexpr int32_t kAlwaysFlyingAltM = 2000;

// A dilution of precision worse than 1.0 makes every velocity less believable,
// so the motion figure is divided by it rather than trusted whole.
constexpr uint16_t kDopUnityE2 = 100;

// INFO: fl 02aug26 The moshe-braner SoftRF fork refuses to accrue takeoff
// evidence from movement that is "too jerky": a speed that jumps by more than
// this ratio between two solutions is GNSS noise at a standstill, not an
// aircraft accelerating (oss/SoftRF-moshe-braner .../src/Wind.cpp:526-620). A
// rejected sample does not count towards the hold, and does not reset it
// either - a real takeoff roll produces one such sample and then behaves.
constexpr int32_t kJerkSpeedRatio = 4;

// A good sample does not wipe the landing hold, it repays half of it, so a
// glider bouncing on the ground with the wing dropping and lifting still lands
// (the fork's counter subtracts two per bad sample and adds one per good one).
constexpr uint32_t kLandingRecoveryDivisor = 2;

// Two solutions further apart than this describe two different flights: a
// receiver that was off, a device that slept. Neither is evidence of anything.
constexpr uint32_t kMaxSampleGapMs = 30000;

// The motion figure the criterion is compared against: horizontal speed plus
// four times the absolute climb, derated by the fix's own DOP.
int32_t motion_e8(const FlightSample& sample);

// Fed every solution, in order. Holds its state through a fix outage rather
// than forgetting it: the aircraft is still where it was.
class FlightMonitor {
   public:
    FlightState update(const FlightSample& sample);

    FlightState state() const { return state_; }
    bool airborne() const { return state_ == FlightState::Airborne; }

   private:
    static bool jerky(uint16_t previous_q, uint16_t now_q);

    FlightState state_{FlightState::Unknown};
    uint32_t hold_ms_{0};
    uint32_t last_ms_{0};
    uint16_t last_speed_q_{0};
    bool armed_{false};
};

}  // namespace skyblip::flight

#endif
