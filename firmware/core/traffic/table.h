// core/traffic/table.h — the fusion / lookout table (roadmap 2.2).
#ifndef SKYBLIP_CORE_TRAFFIC_TABLE_H
#define SKYBLIP_CORE_TRAFFIC_TABLE_H

#include <array>
#include <cstdint>

#include "core/messages/messages.h"

namespace skyblip::traffic {

struct Target {
    messages::AircraftObs obs;
    uint8_t alarm_level{0};
    bool used{false};
};

class TrafficTable {
   public:
    static constexpr int kCapacity = 48;
    static constexpr uint32_t kDefaultMaxAgeSec = 30;

    int update(const messages::AircraftObs& obs, uint32_t now);

    void age_out(uint32_t now, uint32_t max_age = kDefaultMaxAgeSec);

    int count() const;
    const Target* at(int i) const { return (i >= 0 && i < kCapacity) ? &slots_[i] : nullptr; }
    Target* at(int i) { return (i >= 0 && i < kCapacity) ? &slots_[i] : nullptr; }
    void clear();

    int find(uint8_t addr_table, uint32_t addr) const;

   private:
    std::array<Target, kCapacity> slots_{};

    static bool prefer_new(const messages::AircraftObs& incoming,
                           const messages::AircraftObs& existing);
    int allocate_slot(uint32_t now);
};

}

#endif
