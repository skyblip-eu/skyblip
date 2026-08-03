// core/traffic/alarm.h: how close is too close, and when to say so out loud.
//
// Two decisions, deliberately separate. assess() is geometry with no memory:
// where the target is and how fast the gap is actually shutting, from the
// relative velocity vector rather than from the worst case both speeds allow.
// AlarmTracker is the memory - what a target has been doing for the last few
// seconds, and what we last said about it - because one snapshot cannot tell a
// gaggle from a collision course and six seconds of range rate can.
#ifndef SKYBLIP_CORE_TRAFFIC_ALARM_H
#define SKYBLIP_CORE_TRAFFIC_ALARM_H

#include <array>
#include <cstdint>

#include "core/messages/messages.h"

namespace skyblip::traffic {

struct AlarmAssessment {
    uint8_t level{0};
    uint16_t rel_bearing_deg{0};
    int32_t rel_dist_m{0};
    int32_t rel_vert_m{0};
    // Metres per second the range is shrinking at: the relative velocity vector
    // projected on the line of sight. Negative when the target is opening.
    int32_t closing_mps{0};
    bool valid{false};
};

constexpr int32_t kVertWindowM = 300;
constexpr int32_t kInfoDistM = 3000;
constexpr int32_t kImportantDistM = 1500;
constexpr int32_t kUrgentDistM = 500;
constexpr int32_t kUrgentTtiS = 15;
constexpr int32_t kImportantTtiS = 25;

// The range rate two aircraft a kilometre apart show when neither is going
// anywhere near the other: GNSS track and speed noise, and the difference
// between a reported track and the path actually flown. Below it the sign of
// the closure means nothing, so it decides nothing on its own: "urgent" needs
// closure above this floor, and only a target opening faster than it is let out
// of the proximity ring.
constexpr int32_t kClosingFloorMps = 2;

// A target that reports no velocity of its own still has one. Crediting it with
// zero would turn every ADS-B ground station relay and every degraded neighbour
// into a harmless dot, so what is unknown is charged at the speed the aircraft
// this device is built for fly: our own closure along the line of sight, plus
// this as the worst the target can be adding to it.
constexpr int32_t kUnknownTargetSpeedMps = 30;

// No collision in any time a pilot can act on.
constexpr int32_t kNoImpactS = 32767;

AlarmAssessment assess(const messages::OwnState& own, const messages::AircraftObs& target);

// Why a contact that the geometry graded higher is being held down.
enum class Suppression : uint8_t { None, CoCircling, SteadyRange };

// A suppressed contact stays on the screen at "info". Suppression is about the
// annunciator, never about hiding an aircraft.
constexpr uint8_t kSuppressedLevel = 1;

// Thermalling: a glider circles a 20 to 25 s turn, so 14 to 18 deg/s, and a
// full circle in 45 s is the slowest thing still worth calling a circle. Course
// wander in cruise stays well under it.
constexpr int16_t kCirclingTurnDps = 8;
// Two aircraft in the same core turn at the same rate in the same direction.
// The tolerance is wide because both rates come from differentiated GNSS track.
constexpr int16_t kTurnMatchDps = 10;
// One thermal: the core and the circles flown around it, at one working band.
constexpr int32_t kGaggleRangeM = 1000;
constexpr int32_t kGaggleVertM = 100;
// Two aircraft on a common circle have no range rate at all; displaced circles
// and GNSS noise make it swing by a few m/s either way. A pair actually meeting
// head-on at glider speeds closes at 50 to 80, so above this ceiling the
// suppression lets go, whatever both aircraft are doing with their controls.
constexpr int32_t kGaggleClosureMps = 15;

// Six ADS-L solutions at the 1 Hz cadence. A target whose range rate has stayed
// inside the noise floor that long is not arriving: at kClosingFloorMps a
// contact at the edge of the urgent ring is over four minutes away.
constexpr uint32_t kSteadyClosureMs = 6000;

// Long enough for a differentiated track to mean something, short enough that
// the answer is still what the target is doing now.
constexpr uint32_t kTargetTurnWindowMs = 1000;

// INFO: al 02aug26 SoftRF alerts only on targets seen within ALERT_EXPIRATION_TIME
// (5 s) and re-checks no more often than every 2 s
// (oss/SoftRF-lyusupov .../src/TrafficHelper.h:58-59, .../src/TrafficHelper.cpp:236-260).
constexpr uint32_t kAlertMaxAgeMs = 5000;
constexpr uint32_t kRenotifyMs = 2000;

// From "urgent" the annunciator keeps saying it, because at that level the
// pilot is being asked to do something now. Below it, once per escalation.
constexpr uint8_t kReminderLevel = 3;

// One per traffic-table slot, so a full sky still has a memory per target.
constexpr int kTrackedTargets = 48;

// A target ages out of the tracker at the same age the traffic table drops it.
constexpr uint32_t kForgetMs = 30000;

class AlarmTracker {
   public:
    struct Decision {
        AlarmAssessment assessment{};
        Suppression suppression{Suppression::None};
        // The annunciator should speak now: this target has never been
        // announced at this level, or it is urgent and due to be said again.
        bool notify{false};
        // ...and it got worse, which is the only reason to buzz a pocket.
        bool escalated{false};
    };

    Decision update(const messages::OwnState& own, const messages::AircraftObs& target,
                    uint32_t now_ms);

    void forget_stale(uint32_t now_ms);

    // The worst level currently being announced: the highest level said about a
    // target still fresh enough to alert on. It is the assessed level with this
    // tracker's hysteresis already applied - it rises the instant a contact
    // does and falls only once the contact has been calmer for a whole
    // re-notification window - so whatever drives the buzzer reads it here
    // rather than deciding a second time when a level has really gone away.
    uint8_t announced_level(uint32_t now_ms) const;

    int16_t target_turn_dps(uint8_t addr_table, uint32_t addr) const;

   private:
    struct Slot {
        bool used{false};
        uint8_t addr_table{0};
        uint32_t addr{0};
        uint32_t obs_key{0};
        uint32_t seen_ms{0};
        bool turn_armed{false};
        uint32_t turn_ref_ms{0};
        uint16_t turn_ref_track_c9{0};
        int16_t turn_dps{0};
        bool slow_closure{false};
        uint32_t slow_since_ms{0};
        uint8_t notified_level{0};
        uint32_t notified_ms{0};
        bool falling{false};
        uint32_t falling_since_ms{0};
    };

    Slot* slot_for(const messages::AircraftObs& target, uint32_t now_ms);
    static void sample_track(Slot& slot, uint16_t track_c9, uint32_t now_ms);
    static bool co_circling(const Slot& slot, int16_t own_turn_dps, const AlarmAssessment& a);
    static bool notify_for(Slot& slot, uint8_t level, uint32_t now_ms, bool& escalated);

    std::array<Slot, kTrackedTargets> slots_{};
};

}  // namespace skyblip::traffic

#endif
