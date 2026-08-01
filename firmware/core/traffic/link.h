// core/traffic/link.h — what a frame's RSSI says about the transmitter once the
// range the two GNSS receivers agree on is taken back out of it. Comparing raw
// RSSI across emitters compares their distances; comparing implied e.r.p.
// compares their radios.
#ifndef SKYBLIP_CORE_TRAFFIC_LINK_H
#define SKYBLIP_CORE_TRAFFIC_LINK_H

#include <cstdint>

#include "core/messages/messages.h"
#include "core/traffic/table.h"

namespace skyblip::traffic {

// Nearer than this the free-space term stops describing the path (ground bounce,
// near field) and the receiver front end is compressing anyway, so the emitter
// is listed with its range and no verdict on its radio.
constexpr int32_t kMinModelledRangeM = 50;

struct LinkRow {
    uint32_t addr{0};
    messages::Source source{messages::Source::AdslDirect};
    int32_t slant_m{0};
    int32_t up_m{0};
    int8_t rssi_dbm{0};
    int16_t implied_erp_dbm{0};
    // False when the frame was heard over a path that is not the emitter's own:
    // an uplink frame carries the ground station's signal, not the aircraft's.
    bool modelled{false};
};

// Free-space loss on the M-band, whole dB. The model, deliberately: a bare
// free-space reference is what makes an emitter that is 12 dB down visible AS
// 12 dB down, instead of hiding it inside a terrain correction.
int16_t free_space_loss_db(int32_t range_m);

bool estimate_link(const messages::OwnState& own, const messages::AircraftObs& obs, LinkRow& out);

// Nearest first, at most cap rows. Returns how many were filled.
int rank_by_range(const TrafficTable& table, const messages::OwnState& own, LinkRow* out, int cap);

}  // namespace skyblip::traffic

#endif
