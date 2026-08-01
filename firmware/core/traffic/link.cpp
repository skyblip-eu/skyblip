#include "core/traffic/link.h"

#include "core/protocol/nmea_out.h"
#include "core/util/intmath.h"

namespace skyblip::traffic {

namespace {

// 20*log10(f) - 147.55 with f = 868.2 MHz, in tenths of a dB: the constant half
// of free-space loss for range in metres.
constexpr int32_t kMbandFreeSpaceOffsetTenths = 312;

// 20*log10(x) = 6.0206 * log2(x), so the whole thing is one integer log2.
constexpr uint64_t kTwentyLog10PerLog2Tenths = 60206;

// log2 in Q16, by squaring the mantissa one bit at a time. No table, no float,
// and exact enough that a dB is never wrong by more than it is measurable.
uint32_t log2_q16(uint32_t x) {
    if (x < 1) x = 1;
    int b = 0;
    while ((x >> b) > 1) b++;
    uint32_t r = static_cast<uint32_t>(b) << 16;
    if (b > 30) return r;
    uint64_t z = static_cast<uint64_t>(x) << (30 - b);
    for (int i = 1; i <= 16; i++) {
        z = (z * z) >> 30;
        if (z >= (1ULL << 31)) {
            z >>= 1;
            r += 1u << (16 - i);
        }
    }
    return r;
}

bool heard_over_its_own_path(messages::Source s) {
    return s == messages::Source::AdslDirect || s == messages::Source::Alptas;
}

}  // namespace

int16_t free_space_loss_db(int32_t range_m) {
    if (range_m < 1) range_m = 1;
    const uint64_t l2 = log2_q16(static_cast<uint32_t>(range_m));
    const int32_t tenths =
        static_cast<int32_t>(((l2 * kTwentyLog10PerLog2Tenths) / 1000ULL) >> 16) +
        kMbandFreeSpaceOffsetTenths;
    return static_cast<int16_t>((tenths + 5) / 10);
}

bool estimate_link(const messages::OwnState& own, const messages::AircraftObs& obs, LinkRow& out) {
    int32_t north_m = 0, east_m = 0, up_m = 0;
    if (!protocol::relative_ned(own, obs, north_m, east_m, up_m)) return false;

    out = LinkRow{};
    out.addr = obs.addr;
    out.source = obs.source;
    out.up_m = up_m;
    out.rssi_dbm = obs.rssi_dbm;
    const int32_t ground_m = static_cast<int32_t>(idistance(north_m, east_m));
    out.slant_m = static_cast<int32_t>(idistance(ground_m, up_m));

    if (!heard_over_its_own_path(obs.source) || out.slant_m < kMinModelledRangeM) return true;
    out.implied_erp_dbm = static_cast<int16_t>(obs.rssi_dbm + free_space_loss_db(out.slant_m));
    out.modelled = true;
    return true;
}

int rank_by_range(const TrafficTable& table, const messages::OwnState& own, LinkRow* out, int cap) {
    int n = 0;
    for (int i = 0; i < TrafficTable::kCapacity; i++) {
        const Target* t = table.at(i);
        if (!t || !t->used) continue;
        LinkRow row;
        if (!estimate_link(own, t->obs, row)) continue;

        int at = n;
        while (at > 0 && out[at - 1].slant_m > row.slant_m) at--;
        if (at >= cap) continue;
        for (int j = (n < cap ? n : cap - 1); j > at; j--) out[j] = out[j - 1];
        out[at] = row;
        if (n < cap) n++;
    }
    return n;
}

}  // namespace skyblip::traffic
