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

    annunciation::Situation situation{};
    // Not the raw worst: what is being announced, which the tracker already
    // holds through a contact bouncing across a ring boundary, and which falls
    // to nothing when the target that caused it stops being heard.
    situation.level = tracker_.announced_level(now_ms);
    situation.escalated = escalated;
    situation.first_fix = context_.state.own.fix_acquired;
    situation.enabled = context_.state.settings.alarm_enabled;
    situation.running = running_;
    drive(situation, now_ms);

    // Haptics only on the way UP, and only from "important": this device rides in
    // a pocket or a harness where the buzzer is muffled, which is exactly when a
    // pilot needs to feel it. A standing urgent re-announces its tone every
    // couple of seconds and must not pulse the motor with it - escalated is
    // false on a re-notification, which is what keeps the two apart.
    if (!situation.enabled || !running_) return;
    if (escalated && speak >= kVibroFromLevel)
        context_.roles.annunciator.vibrate(speak >= kUrgentLevel ? kVibroUrgentMs
                                                                 : kVibroImportantMs);
}

void AlarmService::park(uint32_t now_ms) {
    running_ = false;
    annunciation::Situation situation{};
    situation.running = false;
    drive(situation, now_ms);
}

void AlarmService::drive(const annunciation::Situation& situation, uint32_t now_ms) {
    const annunciation::Command command = policy_.update(situation, now_ms);
    if (!command.changed) return;
    if (!command.tone_on) {
        context_.roles.annunciator.silence();
        return;
    }
    context_.roles.annunciator.alarm(command.tone_level, context_.state.settings.alarm_volume);
}

}  // namespace skyblip::go
