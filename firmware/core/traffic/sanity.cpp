#include "core/traffic/sanity.h"

#include "core/protocol/nmea_out.h"
#include "core/util/intmath.h"

namespace skyblip::traffic {

int32_t plausible_range_m(messages::Source source) {
    return source == messages::Source::AdslUplink ? kMaxRelayedRangeM : kMaxPlausibleRangeM;
}

Plausibility range_check(const messages::OwnState& own, const messages::AircraftObs& obs,
                         int32_t& slant_m) {
    int32_t north_m = 0, east_m = 0, up_m = 0;
    if (!protocol::relative_ned(own, obs, north_m, east_m, up_m)) return Plausibility::NoReference;

    // The same slant range core/traffic/link.h ranks emitters by: the vertical
    // component is part of the path, and a miscorrected altitude field puts a
    // target tens of kilometres straight up as readily as sideways.
    const int32_t ground_m = static_cast<int32_t>(idistance(north_m, east_m));
    slant_m = static_cast<int32_t>(idistance(ground_m, up_m));
    return slant_m > plausible_range_m(obs.source) ? Plausibility::TooFar
                                                   : Plausibility::Believable;
}

}  // namespace skyblip::traffic
