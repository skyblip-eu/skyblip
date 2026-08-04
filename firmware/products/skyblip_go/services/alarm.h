#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_ALARM_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_ALARM_H

#include "core/annunciation/pattern.h"
#include "core/indication/lamp.h"
#include "core/traffic/alarm.h"
#include "hal/indicator.h"
#include "runtime/service.h"
#include "runtime/tasks.h"

namespace skyblip::go {

// The one owner of everything this product says out loud or shows with a light.
// Nothing else calls alarm(), vibrate(), silence() or show(): the traffic tone,
// the first-fix chirp, the status lamp and the way down all go through the two
// policies in core/annunciation and core/indication, so each output has one
// driver and the rules that arbitrate are each written once.
//
// The lamp lives here rather than in PowerService, whose state most of it reads,
// because the highest-priority row in the table is the alarm level and because a
// device going down has to be silenced and darkened in the same breath - park()
// is already called at exactly the right point in the shutdown sequence.
class AlarmService : public runtime::Service {
   public:
    using runtime::Service::Service;

    void tick(uint32_t now_ms) override;

    // The device is going down: the loop stops running, so the buzzer is
    // released and the lamp is darkened and let go here rather than left latched
    // on a rail that is about to drop.
    void park(uint32_t now_ms);

    // Wired by the product, and only when the board found a lamp. Unattached, the
    // port hal/indicator.h defines IS the absent part: the table below still runs
    // and nothing lights.
    void attach_indicator(hal::Indicator& indicator) { indicator_ = &indicator; }

    bool escalated_since_render() const { return dirty_; }
    void clear_dirty() { dirty_ = false; }

    uint8_t announcing_level() const { return policy_.announcing_level(); }
    bool sounding() const { return policy_.sounding(); }

    // What the table decided, and what the lamp is showing this instant. Both,
    // because they differ in the gaps of a wink and a test has to be able to say
    // which of the two it means.
    indication::Condition indicator_condition() const { return lamp_.condition(); }
    hal::Lamp lamp() const { return lamp_.lamp(); }

   private:
    void drive(const annunciation::Situation& situation, uint32_t now_ms);
    void drive_lamp(uint32_t now_ms, bool running);

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

    // Three passes through the shortest flash in the table. An LED needs far less
    // time than a piezo to be seen, so the figure is smaller than the one above -
    // but a flash the loop cannot resolve is a flash nobody sees.
    static constexpr uint32_t kPassesPerShortestFlash = 3;
    static_assert(runtime::kServiceStepMs * kPassesPerShortestFlash <= indication::kShortestPhaseMs,
                  "the service loop is too coarse to resolve the shortest flash");

    traffic::AlarmTracker tracker_{};
    annunciation::Policy policy_{};
    indication::Policy lamp_{};
    // Never null: what it starts on is the absent lamp, so a product that wired
    // nothing lights nothing rather than dereferencing nothing.
    hal::Indicator absent_lamp_{};
    hal::Indicator* indicator_{&absent_lamp_};
    bool dirty_{false};
    bool running_{true};
};

}  // namespace skyblip::go

#endif
