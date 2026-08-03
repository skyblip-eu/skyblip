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

// How long a first-hand reception keeps a target to itself before a ground
// relay of the same aircraft is allowed to refresh it.
//
// A relay is a rebroadcast: the ground station heard the aircraft, put it in a
// frame with everything else it heard, and sent that on in the next uplink
// slot, so a relayed position is one hop and up to a second older than the
// direct reception it duplicates - and it is a subset of what the direct frame
// carries (no climb rate, no track, a quantised speed). While we are hearing
// the aircraft ourselves, the relay has nothing to add and must not overwrite
// the better report. Once the direct track has gone stale the relay is all
// there is, and it takes over rather than letting the target age out.
//
// The figure is core/traffic/alarm.h's own patience with a contact
// (kAlertMaxAgeMs): a direct report the alarm layer would no longer act on is
// exactly a direct report a relay should be allowed past. test/core/test_traffic.cpp
// pins the two together.
constexpr uint32_t kDirectHoldSec = 5;

class TrafficTable {
   public:
    static constexpr int kCapacity = 48;
    static constexpr uint32_t kDefaultMaxAgeSec = 30;

    // Our own 24-bit address. A ground station relays every aircraft it heard,
    // and it heard us: without this the uplink puts own-ship on the radar, at
    // own-ship's position, and the alarm layer grades a head-on with the
    // aircraft it is bolted to. Zero means nothing is filtered, which is what a
    // table nobody told has to assume.
    void set_own_address(uint32_t addr) { own_addr_ = addr & 0x00FFFFFF; }

    int update(const messages::AircraftObs& obs, uint32_t now);

    void age_out(uint32_t now, uint32_t max_age = kDefaultMaxAgeSec);

    int count() const;
    const Target* at(int i) const { return (i >= 0 && i < kCapacity) ? &slots_[i] : nullptr; }
    Target* at(int i) { return (i >= 0 && i < kCapacity) ? &slots_[i] : nullptr; }
    void clear();

    int find(uint8_t addr_table, uint32_t addr) const;

   private:
    std::array<Target, kCapacity> slots_{};
    uint32_t own_addr_{0};

    static bool prefer_new(const messages::AircraftObs& incoming,
                           const messages::AircraftObs& existing);
    int allocate_slot(uint32_t now);
};

}

#endif
