#include "core/protocol/air.h"

#include "core/fec/manchester.h"

namespace skyblip::protocol {

namespace {

constexpr uint32_t kTailMask = sync_tail(0xFFFFFFFFu);
// A dead chip pair decodes to a wrong bit, and the tail is only 23 of them.
constexpr int kMaxSyncTailErrors = 1;

uint32_t tail_of(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 16) | (static_cast<uint32_t>(data[1]) << 8) | data[2];
}

int tail_errors(const uint8_t* data, uint32_t sync_word) {
    return __builtin_popcount((tail_of(data) ^ sync_tail(sync_word)) & kTailMask);
}

System system_of(const uint8_t* data) {
    if (tail_errors(data, kAdslSyncWord) <= kMaxSyncTailErrors) return System::AdslDirect;
    if (tail_errors(data, kAlptasSyncWord) <= kMaxSyncTailErrors) return System::Alptas;
    return System::Unknown;
}

void shift_left(uint8_t* bytes, size_t len, uint8_t bits) {
    const size_t byte_offset = bits >> 3;
    const uint8_t rest = bits & 7;
    for (size_t i = 0; i < len; i++) {
        const size_t src = i + byte_offset;
        const uint32_t high = src < len ? bytes[src] : 0u;
        const uint32_t low = src + 1 < len ? bytes[src + 1] : 0u;
        bytes[i] = static_cast<uint8_t>((high << rest) | (rest != 0 ? low >> (8u - rest) : 0u));
    }
}

}  // namespace

uint8_t frame_bytes(System system) {
    switch (system) {
        case System::AdslDirect: return kAdslFrameBytes;
        case System::Alptas: return kAlptasFrameBytes;
        case System::AdslUplink: return kUplinkFrameBytes;
        case System::Unknown: return 0;
    }
    return 0;
}

size_t encode_mband(uint32_t sync_word, const uint8_t* frame, uint8_t frame_len, uint8_t* chips) {
    uint8_t sync[4] = {0};
    for (int i = 0; i < 4; i++) sync[i] = static_cast<uint8_t>(sync_word >> (24 - 8 * i));
    fec::manchester_encode(sync, sizeof(sync), chips);
    fec::manchester_encode(frame, frame_len, chips + kSyncChipBytes);
    return kSyncChipBytes + 2u * frame_len;
}

bool receive_mband(const uint8_t* chips, size_t chip_bytes, Frame& out) {
    out = Frame{};
    if (chip_bytes < kRxChipBytes) return false;
    fec::manchester_decode(chips, kRxFrameBytes, out.data, out.err);
    out.system = system_of(out.data);
    if (out.system == System::Unknown) return false;
    shift_left(out.data, kRxFrameBytes, kSyncTailBits);
    shift_left(out.err, kRxFrameBytes, kSyncTailBits);
    out.len = frame_bytes(out.system);
    // A dwell reads for the longer of the two frames, so the shorter one is
    // followed by chips no transmitter sent. Counting those as damage would call
    // every ADS-L frame broken.
    for (uint8_t i = 0; i < out.len; i++)
        out.bad_chips = static_cast<uint16_t>(out.bad_chips + __builtin_popcount(out.err[i]));
    return true;
}

size_t encode_oband(const uint8_t* frame, uint8_t* out) {
    for (size_t i = 0; i < sizeof(kUplinkSync); i++) out[i] = kUplinkSync[i];
    for (size_t i = 0; i < kUplinkFrameBytes; i++) out[sizeof(kUplinkSync) + i] = frame[i];
    return sizeof(kUplinkSync) + kUplinkFrameBytes;
}

System receive_burst(messages::Band band, const uint8_t* data, size_t len, Frame& out) {
    if (band == messages::Band::O) {
        out = Frame{};
        return len == kUplinkFrameBytes ? System::AdslUplink : System::Unknown;
    }
    return receive_mband(data, len, out) ? out.system : System::Unknown;
}

}  // namespace skyblip::protocol
