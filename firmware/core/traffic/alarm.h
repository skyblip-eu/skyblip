// core/traffic/alarm.h — collision alarm assessment (roadmap 2.2, T2).
#ifndef SKYBLIP_CORE_TRAFFIC_ALARM_H
#define SKYBLIP_CORE_TRAFFIC_ALARM_H

#include <cstdint>

#include "core/messages/messages.h"

namespace skyblip::traffic {

struct AlarmAssessment {
    uint8_t level{0};
    uint16_t rel_bearing_deg{0};
    int32_t rel_dist_m{0};
    int32_t rel_vert_m{0};
    int32_t closing_mps{0};
    bool valid{false};
};

constexpr int32_t kVertWindowM = 300;
constexpr int32_t kInfoDistM = 3000;
constexpr int32_t kImportantDistM = 1500;
constexpr int32_t kUrgentDistM = 500;
constexpr int32_t kUrgentTtiS = 15;
constexpr int32_t kImportantTtiS = 25;

AlarmAssessment assess(const messages::OwnState& own, const messages::AircraftObs& target);

}

#endif
