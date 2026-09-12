#include "core/radio/log.h"

namespace skyblip::radio {

void Log::record(const Entry& entry) {
    entry_[written_ % kCapacity] = entry;
    written_++;
}

const Entry& Log::newest(int i) const {
    const int held = count();
    if (held == 0) return entry_[0];
    const int back = i < 0 ? 0 : (i >= held ? held - 1 : i);
    return entry_[(written_ - 1u - static_cast<uint32_t>(back)) % kCapacity];
}

}  // namespace skyblip::radio
