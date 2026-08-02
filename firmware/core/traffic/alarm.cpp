#include "core/traffic/alarm.h"

#include "core/flight/turn.h"
#include "core/protocol/nmea_out.h"
#include "core/util/intmath.h"

namespace skyblip::traffic {

namespace {

// isin/icos are Q14, and speed_q is quarter-metres per second, so a velocity
// component here is quarter-metres per second scaled by kTrigOne.
constexpr int32_t kTrigOne = 16384;
constexpr int32_t kSpeedQPerMps = 4;
// cordic9: 512 units of track for 65536 units of angle.
constexpr int kTrackC9ToAngle = 7;
constexpr int kTrackC9Mask = 0x1FF;

void velocity_ned(uint16_t speed_q, uint16_t track_c9, int32_t& north, int32_t& east) {
    const int16_t angle =
        static_cast<int16_t>(static_cast<uint16_t>((track_c9 & kTrackC9Mask) << kTrackC9ToAngle));
    north = static_cast<int32_t>(speed_q) * icos(angle);
    east = static_cast<int32_t>(speed_q) * isin(angle);
}

int32_t closing_from_vectors(const messages::OwnState& own, const messages::AircraftObs& target,
                             int32_t n_m, int32_t e_m, int32_t dist_m) {
    if (dist_m <= 0) return kUnknownTargetSpeedMps;

    int32_t own_n = 0, own_e = 0;
    velocity_ned(own.speed_q, own.track_c9, own_n, own_e);
    int32_t target_n = 0, target_e = 0;
    if (target.has_speed) velocity_ned(target.speed_q, target.track_c9, target_n, target_e);

    const int64_t along =
        static_cast<int64_t>(target_n - own_n) * n_m + static_cast<int64_t>(target_e - own_e) * e_m;
    const int64_t scale = static_cast<int64_t>(dist_m) * kSpeedQPerMps * kTrigOne;
    int32_t closing = static_cast<int32_t>(-along / scale);
    if (!target.has_speed) closing += kUnknownTargetSpeedMps;
    return closing;
}

int32_t iabs32(int32_t v) { return v < 0 ? -v : v; }

}  // namespace

AlarmAssessment assess(const messages::OwnState& own, const messages::AircraftObs& target) {
    AlarmAssessment a{};
    int32_t n_m, e_m, u_m;
    if (!protocol::relative_ned(own, target, n_m, e_m, u_m)) return a;
    a.valid = true;
    a.rel_vert_m = u_m;
    a.rel_dist_m = static_cast<int32_t>(idistance(n_m, e_m));
    a.closing_mps = closing_from_vectors(own, target, n_m, e_m, a.rel_dist_m);

    int16_t brg = iatan2(e_m, n_m);
    int own_deg = (static_cast<int>(own.track_c9) * 45) >> 6;
    int brg_deg = (static_cast<int>(static_cast<uint16_t>(brg)) * 360) / 65536;
    int rel = ((brg_deg - own_deg) % 360 + 360) % 360;
    a.rel_bearing_deg = static_cast<uint16_t>(rel);

    if (u_m > kVertWindowM || u_m < -kVertWindowM) return a;

    const bool converging = a.closing_mps >= kClosingFloorMps;
    const bool opening = a.closing_mps <= -kClosingFloorMps;
    const int32_t tti = converging ? a.rel_dist_m / a.closing_mps : kNoImpactS;

    uint8_t level = 0;
    if (a.rel_dist_m <= kInfoDistM) level = 1;
    if (!opening && (a.rel_dist_m <= kImportantDistM || tti <= kImportantTtiS)) level = 2;
    if (converging && (a.rel_dist_m <= kUrgentDistM || tti <= kUrgentTtiS)) level = 3;
    a.level = level;
    return a;
}

AlarmTracker::Decision AlarmTracker::update(const messages::OwnState& own,
                                            const messages::AircraftObs& target, uint32_t now_ms) {
    Decision d{};
    d.assessment = assess(own, target);
    if (!d.assessment.valid) return d;

    Slot* slot = slot_for(target, now_ms);
    if (slot == nullptr) return d;

    const uint32_t key = target.rx_utc * 1000u + target.rx_ms;
    if (key != slot->obs_key) {
        slot->obs_key = key;
        slot->seen_ms = now_ms;
        if (target.has_speed) sample_track(*slot, target.track_c9, now_ms);
    }

    if (d.assessment.closing_mps >= kClosingFloorMps) {
        slot->slow_closure = false;
    } else if (!slot->slow_closure) {
        slot->slow_closure = true;
        slot->slow_since_ms = now_ms;
    }

    if (co_circling(*slot, own.turn_dps, d.assessment))
        d.suppression = Suppression::CoCircling;
    else if (slot->slow_closure && now_ms - slot->slow_since_ms >= kSteadyClosureMs)
        d.suppression = Suppression::SteadyRange;

    if (d.suppression != Suppression::None && d.assessment.level > kSuppressedLevel)
        d.assessment.level = kSuppressedLevel;

    if (now_ms - slot->seen_ms <= kAlertMaxAgeMs)
        d.notify = notify_for(*slot, d.assessment.level, now_ms, d.escalated);
    return d;
}

// A level is announced once. It is said again only while it is urgent, and the
// level we last said falls back only after the contact has been calmer than it
// for a whole re-notification window, so a target oscillating across a ring
// boundary is announced once, not twice a second.
bool AlarmTracker::notify_for(Slot& slot, uint8_t level, uint32_t now_ms, bool& escalated) {
    bool speak = false;
    if (level > slot.notified_level) {
        speak = true;
        escalated = true;
    } else if (level == slot.notified_level && level >= kReminderLevel &&
               now_ms - slot.notified_ms >= kRenotifyMs) {
        speak = true;
    }

    if (level < slot.notified_level) {
        if (!slot.falling) {
            slot.falling = true;
            slot.falling_since_ms = now_ms;
        } else if (now_ms - slot.falling_since_ms >= kRenotifyMs) {
            slot.notified_level = level;
            slot.falling = false;
        }
    } else {
        slot.falling = false;
    }

    if (speak) {
        slot.notified_level = level;
        slot.notified_ms = now_ms;
    }
    return speak;
}

bool AlarmTracker::co_circling(const Slot& slot, int16_t own_turn_dps, const AlarmAssessment& a) {
    if (iabs32(own_turn_dps) < kCirclingTurnDps) return false;
    if (iabs32(slot.turn_dps) < kCirclingTurnDps) return false;
    if ((own_turn_dps > 0) != (slot.turn_dps > 0)) return false;
    if (iabs32(own_turn_dps - slot.turn_dps) > kTurnMatchDps) return false;
    if (a.rel_dist_m > kGaggleRangeM) return false;
    if (iabs32(a.rel_vert_m) > kGaggleVertM) return false;
    return a.closing_mps <= kGaggleClosureMps;
}

void AlarmTracker::sample_track(Slot& slot, uint16_t track_c9, uint32_t now_ms) {
    if (!slot.turn_armed) {
        slot.turn_armed = true;
        slot.turn_ref_ms = now_ms;
        slot.turn_ref_track_c9 = track_c9;
        return;
    }
    const uint32_t dt = now_ms - slot.turn_ref_ms;
    if (dt < kTargetTurnWindowMs) return;
    slot.turn_dps = flight::turn_rate_dps(track_c9, slot.turn_ref_track_c9, dt);
    slot.turn_ref_ms = now_ms;
    slot.turn_ref_track_c9 = track_c9;
}

AlarmTracker::Slot* AlarmTracker::slot_for(const messages::AircraftObs& target, uint32_t now_ms) {
    Slot* free_slot = nullptr;
    Slot* oldest = nullptr;
    for (Slot& s : slots_) {
        if (s.used && s.addr == target.addr && s.addr_table == target.addr_table) return &s;
        if (!s.used) {
            if (free_slot == nullptr) free_slot = &s;
            continue;
        }
        if (oldest == nullptr || now_ms - s.seen_ms > now_ms - oldest->seen_ms) oldest = &s;
    }
    Slot* slot = free_slot != nullptr ? free_slot : oldest;
    if (slot == nullptr) return nullptr;
    *slot = Slot{};
    slot->used = true;
    slot->addr = target.addr;
    slot->addr_table = target.addr_table;
    slot->seen_ms = now_ms;
    return slot;
}

void AlarmTracker::forget_stale(uint32_t now_ms) {
    for (Slot& s : slots_) {
        if (s.used && now_ms - s.seen_ms > kForgetMs) s = Slot{};
    }
}

int16_t AlarmTracker::target_turn_dps(uint8_t addr_table, uint32_t addr) const {
    for (const Slot& s : slots_) {
        if (s.used && s.addr == addr && s.addr_table == addr_table) return s.turn_dps;
    }
    return 0;
}

}  // namespace skyblip::traffic
