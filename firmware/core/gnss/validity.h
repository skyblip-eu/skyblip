// core/gnss/validity.h: whether the solution the parser holds is a fix.
//
// The parser answers "what did the last sentence say". That is not the same
// question. A receiver that said `A` once and then went silent still says `A`
// forever, a receiver that lost its GGA still has a position, and a receiver
// that teleports 200 km still has a checksum. SoftRF requires GGA and RMC both
// present with every age inside 3500 ms (oss/SoftRF-lyusupov
// .../src/driver/GNSS.cpp:1487-1503) and moshe-braner adds a jump gate
// (.../SoftRF.ino:517-523). This is that rule, in one object, so a fix that
// fails it is not a fix anywhere downstream.
#ifndef SKYBLIP_CORE_GNSS_VALIDITY_H
#define SKYBLIP_CORE_GNSS_VALIDITY_H

#include <cstdint>

#include "core/gnss/nmea.h"

namespace skyblip::gnss {

// SoftRF's NMEA_EXP_TIME. Our receiver is configured for 5 Hz, so this is
// seventeen missed solutions: it is a liveness bound, not a freshness one.
// Freshness for transmission is timing::Transmitter::kFixAgeMaxMs, 500 ms, and
// it is a separate and stricter gate applied at the point of transmission.
constexpr uint32_t kSentenceMaxAgeMs = 3500;

// moshe-braner's jump gate, in our units: 0.15 deg of latitude and 0.25 deg of
// longitude between consecutive solutions. At 1 Hz that is 16.7 km of latitude
// in a second, which no aircraft we serve can do and no correct receiver
// reports.
constexpr int32_t kMaxLatitudeJump1e7 = 1500000;
constexpr int32_t kMaxLongitudeJump1e7 = 2500000;

// Why a solution is not a fix. Every one of these has been a support case in one
// of the reference projects, which is why they are named and counted rather than
// folded into a bool.
enum class FixReject : uint8_t {
    None,
    NoSolution,  // RMC said V, or GGA reported no usable quality
    MissingRmc,  // never seen one: position and date come from it
    MissingGga,  // never seen one: altitude, quality and HDOP come from it
    Stale,       // one of them stopped arriving
    NoDate,      // the MTK 1980 lie, or a receiver with no almanac yet
    Jump,        // a position no aircraft could have flown to
};

// GGA field 6. SoftRF accepts GPS through float RTK and nothing else: 0 is no
// fix and 6 is dead reckoning, which is a receiver guessing.
constexpr uint8_t kQualityGps = 1;
constexpr uint8_t kQualityFloatRtk = 5;

class FixValidity {
   public:
    // One accepted sentence, with the instant it arrived.
    void observe(const GnssFix& fix, Sentence which, uint32_t now_ms);

    // The verdict, without side effects.
    FixReject evaluate(uint32_t now_ms) const;
    bool valid(uint32_t now_ms) const { return evaluate(now_ms) == FixReject::None; }

    // The same verdict, counted: one event per run of the same reason. This is
    // the one a driver calls once per poll.
    FixReject check(uint32_t now_ms);

    // The last reason a solution was refused, and how many have been refused
    // since boot. Support reads both; nothing else does.
    FixReject last_reject() const { return last_reject_; }
    uint32_t rejected() const { return rejected_; }

    void reset();

   private:
    uint32_t rmc_ms_{0};
    uint32_t gga_ms_{0};
    int32_t prev_lat_1e7_{0};
    int32_t prev_lon_1e7_{0};
    uint32_t rejected_{0};
    FixReject last_reject_{FixReject::None};
    bool have_rmc_{false};
    bool have_gga_{false};
    bool rmc_solution_{false};
    bool gga_solution_{false};
    bool date_ok_{false};
    bool jumped_{false};
    bool have_previous_{false};
};

}  // namespace skyblip::gnss

#endif
