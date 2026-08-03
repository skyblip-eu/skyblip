#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_ALARM_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_ALARM_H

#include "core/annunciation/pattern.h"
#include "core/traffic/alarm.h"
#include "runtime/service.h"
#include "runtime/tasks.h"

namespace skyblip::go {

// The one owner of the annunciator on this product. Nothing else calls alarm(),
// vibrate() or silence(): the traffic tone, the first-fix chirp and the way
// down all go through the policy in core/annunciation, so the buzzer has one
// driver and the rule that arbitrates between them is written once.
class AlarmService : public runtime::Service {
   public:
    using runtime::Service::Service;

    void tick(uint32_t now_ms) override;

    // The device is going down: the loop stops running, so the buzzer is
    // released here rather than left latched on a rail that is about to drop.
    void park(uint32_t now_ms);

    bool escalated_since_render() const { return dirty_; }
    void clear_dirty() { dirty_ = false; }

    uint8_t announcing_level() const { return policy_.announcing_level(); }
    bool sounding() const { return policy_.sounding(); }

   private:
    void drive(const annunciation::Situation& situation, uint32_t now_ms);

    // core/traffic/alarm.h grades contacts 1 info, 2 important, 3 urgent.
    static constexpr uint8_t kVibroFromLevel = 2;
    static constexpr uint8_t kUrgentLevel = 3;
    // Long enough to feel through a glove and a harness strap, short enough not
    // to blur into the next escalation.
    static constexpr uint16_t kVibroImportantMs = 200;
    static constexpr uint16_t kVibroUrgentMs = 600;

    // Eight passes through the shortest phase of the fastest pattern: the
    // cadence the ear gets is the cadence written in core/annunciation, to
    // within one pass of the loop.
    static constexpr uint32_t kPassesPerShortestPhase = 8;
    static_assert(runtime::kServiceStepMs * kPassesPerShortestPhase <=
                      annunciation::kShortestPhaseMs,
                  "the service loop is too coarse to resolve the urgent pulse train");

    traffic::AlarmTracker tracker_{};
    annunciation::Policy policy_{};
    bool dirty_{false};
    bool running_{true};
};

}  // namespace skyblip::go

#endif
