#ifndef SKYBLIP_CORE_RADIO_LOG_H
#define SKYBLIP_CORE_RADIO_LOG_H

#include <cstdint>

#include "core/messages/messages.h"

namespace skyblip::radio {

enum class Event : uint8_t { Transmitted, Withheld, Lost, Received, Unframed };

struct Entry {
    Event event{Event::Unframed};
    messages::Band band{messages::Band::M};
    messages::Source source{messages::Source::AdslDirect};
    uint32_t addr{0};
    uint32_t at_s{0};
    int8_t rssi_dbm{0};
    bool rssi_valid{false};
    bool utc{false};
};

class Log {
   public:
    static constexpr int kCapacity = 16;

    void record(const Entry& entry);
    void clear() { written_ = 0; }

    int count() const { return written_ < kCapacity ? static_cast<int>(written_) : kCapacity; }
    const Entry& newest(int i) const;

   private:
    Entry entry_[kCapacity]{};
    uint32_t written_{0};
};

}  // namespace skyblip::radio

#endif
