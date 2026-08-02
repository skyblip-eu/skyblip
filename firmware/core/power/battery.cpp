#include "core/power/battery.h"

namespace skyblip::power {

namespace {

struct Point {
    uint16_t millivolts;
    uint8_t percent;
};

// INFO: fc 09mar26 both curves are the textbook shape for a single Li-ion cell,
// not this pack measured on this board. They are right to a few percent in the
// middle and worst at the ends, which is where a pilot cares. Replace them with a
// discharge log from a unit when there is one, the shape of the table does not
// change.
//
// A single Li-ion cell off charge, under the load this device draws. The middle
// is flat and the ends are steep, which is why a straight line from 3.3 to 4.2 V
// reads half full for most of a flight and then falls off a cliff.
constexpr Point kDischargeCurve[] = {
    {3300, 0},  {3500, 5},  {3600, 12}, {3700, 25}, {3750, 40}, {3800, 55},
    {3850, 65}, {3900, 75}, {3950, 83}, {4000, 89}, {4100, 95}, {4200, 100},
};

// The same cell on charge reads higher for the same state of charge: the
// constant-current phase lifts the terminal by the drop across the cell's
// internal resistance, and the 4.15..4.20 V constant-voltage taper is where the
// last fifth of the capacity goes in, at a voltage that barely moves.
constexpr Point kChargeCurve[] = {
    {3400, 0},  {3600, 5},  {3700, 12}, {3800, 25}, {3900, 40}, {4000, 55},
    {4050, 65}, {4100, 75}, {4150, 82}, {4180, 90}, {4190, 95}, {4200, 100},
};

uint16_t median_of(uint16_t a, uint16_t b, uint16_t c) {
    if (a > b) {
        const uint16_t swap = a;
        a = b;
        b = swap;
    }
    if (b > c) b = c > a ? c : a;
    return b;
}

template <int N>
uint8_t percent_on(const Point (&curve)[N], uint16_t millivolts) {
    if (millivolts <= curve[0].millivolts) return curve[0].percent;
    for (int i = 1; i < N; i++) {
        if (millivolts > curve[i].millivolts) continue;
        const Point& low = curve[i - 1];
        const Point& high = curve[i];
        const int32_t span_mv = high.millivolts - low.millivolts;
        const int32_t span_percent = high.percent - low.percent;
        const int32_t into = millivolts - low.millivolts;
        return static_cast<uint8_t>(low.percent + (into * span_percent + span_mv / 2) / span_mv);
    }
    return curve[N - 1].percent;
}

}  // namespace

uint8_t percent_from_mv(uint16_t millivolts, bool charging) {
    return charging ? percent_on(kChargeCurve, millivolts)
                    : percent_on(kDischargeCurve, millivolts);
}

void Gauge::apply(const messages::BatterySample& sample) {
    for (int i = kWindow - 1; i > 0; i--) recent_[i] = recent_[i - 1];
    recent_[0] = sample.millivolts;
    if (seen_ < kWindow) seen_++;

    // Rejecting a transient takes three readings. Before that the newest is
    // everything the gauge knows.
    const uint16_t millivolts =
        seen_ < kWindow ? recent_[0] : median_of(recent_[0], recent_[1], recent_[2]);
    const bool was_charging = state_.charging;
    const bool charging = sample.external_power && millivolts < kChargeCompleteMv;
    const uint8_t percent = percent_from_mv(millivolts, charging);

    // Unplugging swaps the curve under the reading, and the cell relaxes upwards
    // once the charge current stops: both are direction changes, so the gauge
    // re-seats on the new curve instead of holding the old number.
    const bool reseat = !state_.valid || charging != was_charging;
    if (reseat)
        state_.percent = percent;
    else if (charging)
        state_.percent = percent > state_.percent ? percent : state_.percent;
    else if (!sample.external_power)
        state_.percent = percent < state_.percent ? percent : state_.percent;
    else
        state_.percent = percent;

    state_.millivolts = millivolts;
    state_.external_power = sample.external_power;
    state_.charging = charging;
    state_.valid = true;
}

}  // namespace skyblip::power
