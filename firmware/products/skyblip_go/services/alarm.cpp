#include "products/skyblip_go/services/alarm.h"

namespace skyblip::go {

void AlarmService::tick(uint32_t now_ms) {
    uint8_t worst = 0;
    uint8_t speak = 0;
    bool escalated = false;
    if (context_.state.own.fix_valid) {
        for (int i = 0; i < traffic::TrafficTable::kCapacity; i++) {
            traffic::Target* t = context_.state.traffic.at(i);
            if (!t || !t->used) continue;
            const traffic::AlarmTracker::Decision d =
                tracker_.update(context_.state.own, t->obs, now_ms);
            t->alarm_level = d.assessment.level;
            if (d.assessment.level > worst) worst = d.assessment.level;
            if (!d.notify) continue;
            if (d.assessment.level > speak) speak = d.assessment.level;
            escalated = escalated || d.escalated;
        }
    }
    tracker_.forget_stale(now_ms);

    if (worst != context_.state.alarm_level) {
        context_.state.alarm_level = worst;
        dirty_ = true;
    }
    if (!context_.state.settings.alarm_enabled) return;

    if (worst == 0) {
        if (!sounding_) return;
        sounding_ = false;
        context_.roles.annunciator.silence();
        return;
    }
    if (speak == 0) return;

    context_.roles.annunciator.alarm(speak, context_.state.settings.alarm_volume);
    sounding_ = true;

    // Haptics only on the way UP, and only from "important": this device rides in
    // a pocket or a harness where the buzzer is muffled, which is exactly when a
    // pilot needs to feel it.
    if (escalated && speak >= kVibroFromLevel)
        context_.roles.annunciator.vibrate(speak >= kUrgentLevel ? kVibroUrgentMs
                                                                 : kVibroImportantMs);
}

}  // namespace skyblip::go
