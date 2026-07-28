#include "products/skyblip_go/services/alarm.h"

#include "core/traffic/alarm.h"

namespace skyblip::go {

void AlarmService::tick(uint32_t) {
    uint8_t worst = 0;
    if (context_.state.own.fix_valid) {
        for (int i = 0; i < traffic::TrafficTable::kCapacity; i++) {
            traffic::Target* t = context_.state.traffic.at(i);
            if (!t || !t->used) continue;
            const traffic::AlarmAssessment a = traffic::assess(context_.state.own, t->obs);
            t->alarm_level = a.level;
            if (a.level > worst) worst = a.level;
        }
    }
    if (worst == context_.state.alarm_level) return;

    const bool escalated = worst > context_.state.alarm_level;
    context_.state.alarm_level = worst;
    dirty_ = true;
    if (!context_.state.settings.alarm_enabled) return;

    if (worst > 0)
        context_.roles.annunciator.alarm(worst, context_.state.settings.alarm_volume);
    else
        context_.roles.annunciator.silence();

    // Haptics only on the way UP, and only from "important": this device rides in
    // a pocket or a harness where the buzzer is muffled, which is exactly when a
    // pilot needs to feel it.
    if (escalated && worst >= kVibroFromLevel)
        context_.roles.annunciator.vibrate(worst >= kUrgentLevel ? kVibroUrgentMs
                                                                 : kVibroImportantMs);
}

}  // namespace skyblip::go
