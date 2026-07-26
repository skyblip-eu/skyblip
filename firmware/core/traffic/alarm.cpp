#include "core/traffic/alarm.h"

#include "core/protocol/nmea_out.h"
#include "core/util/intmath.h"

namespace skyblip::traffic {

AlarmAssessment assess(const messages::OwnState& own, const messages::AircraftObs& target) {
    AlarmAssessment a{};
    int32_t n_m, e_m, u_m;
    if (!protocol::relative_ned(own, target, n_m, e_m, u_m)) return a;
    a.valid = true;
    a.rel_vert_m = u_m;
    a.rel_dist_m = static_cast<int32_t>(idistance(n_m, e_m));

    int16_t brg = iatan2(e_m, n_m);
    int own_deg = (static_cast<int>(own.track_c9) * 45) >> 6;
    int brg_deg = (static_cast<int>(static_cast<uint16_t>(brg)) * 360) / 65536;
    int rel = ((brg_deg - own_deg) % 360 + 360) % 360;
    a.rel_bearing_deg = static_cast<uint16_t>(rel);

    if (u_m > kVertWindowM || u_m < -kVertWindowM) {
        a.level = 0;
        return a;
    }

    int32_t own_mps = static_cast<int32_t>(own.speed_q) / 4;
    int32_t tgt_mps = target.has_speed ? static_cast<int32_t>(target.speed_q) / 4 : 0;
    a.closing_mps = own_mps + tgt_mps;
    if (a.closing_mps < 1) a.closing_mps = 1;
    int32_t tti = a.rel_dist_m / a.closing_mps;

    uint8_t level = 0;
    if (a.rel_dist_m <= kInfoDistM) level = 1;
    if (a.rel_dist_m <= kImportantDistM || tti <= kImportantTtiS) level = 2;
    if (a.rel_dist_m <= kUrgentDistM || tti <= kUrgentTtiS) level = 3;
    a.level = level;
    return a;
}

}
