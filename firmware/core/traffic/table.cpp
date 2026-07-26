#include "core/traffic/table.h"

namespace skyblip::traffic {

namespace {
uint32_t obs_time(const messages::AircraftObs& o) { return o.rx_utc; }
int source_rank(messages::Source s) {
    switch (s) {
        case messages::Source::AdslDirect: return 3;
        case messages::Source::Alptas: return 3;
        case messages::Source::AdslUplink: return 1;
        default: return 0;
    }
}
}

int TrafficTable::find(uint8_t addr_table, uint32_t addr) const {
    for (int i = 0; i < kCapacity; i++) {
        if (slots_[i].used && slots_[i].obs.addr == addr && slots_[i].obs.addr_table == addr_table)
            return i;
    }
    return -1;
}

bool TrafficTable::prefer_new(const messages::AircraftObs& in, const messages::AircraftObs& ex) {
    uint32_t tin = obs_time(in), tex = obs_time(ex);
    if (tin != tex) return tin > tex;
    return source_rank(in.source) >= source_rank(ex.source);
}

int TrafficTable::allocate_slot(uint32_t now) {
    for (int i = 0; i < kCapacity; i++)
        if (!slots_[i].used) return i;
    int victim = -1;
    uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < kCapacity; i++) {
        if (slots_[i].alarm_level > 0) continue;
        uint32_t age = now - obs_time(slots_[i].obs);
        if (age >= oldest || victim < 0) {
            if (victim < 0 || age > oldest) {
                oldest = age;
                victim = i;
            }
        }
    }
    return victim;
}

int TrafficTable::update(const messages::AircraftObs& obs, uint32_t now) {
    (void)now;
    int idx = find(obs.addr_table, obs.addr);
    if (idx >= 0) {
        if (prefer_new(obs, slots_[idx].obs)) slots_[idx].obs = obs;
        return idx;
    }
    idx = allocate_slot(now);
    if (idx < 0) return -1;
    slots_[idx].used = true;
    slots_[idx].obs = obs;
    slots_[idx].alarm_level = 0;
    return idx;
}

void TrafficTable::age_out(uint32_t now, uint32_t max_age) {
    for (int i = 0; i < kCapacity; i++) {
        if (!slots_[i].used) continue;
        if (now - obs_time(slots_[i].obs) > max_age) {
            slots_[i].used = false;
            slots_[i].alarm_level = 0;
        }
    }
}

int TrafficTable::count() const {
    int n = 0;
    for (int i = 0; i < kCapacity; i++)
        if (slots_[i].used) n++;
    return n;
}

void TrafficTable::clear() {
    for (auto& s : slots_) {
        s.used = false;
        s.alarm_level = 0;
    }
}

}
