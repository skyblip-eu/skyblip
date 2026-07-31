// core/protocol/air.h — one M-band dwell, two systems: ADS-L 4 SRD-860 issue 2
// §C.2 and the FLARM-generation wire this project calls ALP-TAS. Both Manchester
// their frames behind a 32-bit sync word, and the two sync words agree on their
// first 18 chips, so a receiver armed with 16 of those chips detects either one
// and software names the system from the chips that follow.
#ifndef SKYBLIP_CORE_PROTOCOL_AIR_H
#define SKYBLIP_CORE_PROTOCOL_AIR_H

#include <cstddef>
#include <cstdint>

#include "core/protocol/adsl.h"
#include "core/protocol/alptas.h"

namespace skyblip::protocol {

enum class System : uint8_t { Unknown, AdslDirect, Alptas };

// ADS-L spends its last sync byte on the frame length, 0x18 = 24 data bytes.
constexpr uint32_t kAdslSyncWord = 0xF5724B18u;
constexpr uint32_t kAlptasSyncWord = 0xF531FAB6u;

constexpr uint8_t kAdslFrameBytes = AdslPacket::kDataBytes;

// One data bit becomes two chips: 1 -> 01, 0 -> 10 (§C.2.1), which is the table
// fec::manchester_encode holds.
constexpr uint16_t manchester_chips(uint8_t byte) {
    uint16_t chips = 0;
    for (int bit = 7; bit >= 0; bit--) {
        chips = static_cast<uint16_t>(chips << 2);
        chips = static_cast<uint16_t>(chips | (((byte >> bit) & 1u) ? 0x1u : 0x2u));
    }
    return chips;
}

// A sync detector slides over chips, so the window it matches need not start at
// the first one. Skipping two chips is what makes the shared window 16 chips
// long and byte-aligned, which is the longest one a radio can be handed.
constexpr uint8_t kSharedSyncSkipChips = 2;
constexpr uint8_t kSharedSyncBits = 16;
constexpr uint8_t kSharedSyncEndChip = kSharedSyncSkipChips + kSharedSyncBits;

constexpr uint16_t shared_sync(uint32_t sync_word) {
    const uint32_t first = manchester_chips(static_cast<uint8_t>(sync_word >> 24));
    const uint32_t second = manchester_chips(static_cast<uint8_t>(sync_word >> 16));
    return static_cast<uint16_t>(((first << 16) | second) >> (32u - kSharedSyncEndChip));
}

static_assert(shared_sync(kAdslSyncWord) == shared_sync(kAlptasSyncWord),
              "the shared chips are what let one dwell hear both systems");

constexpr uint8_t kSharedSync[2] = {static_cast<uint8_t>(shared_sync(kAdslSyncWord) >> 8),
                                    static_cast<uint8_t>(shared_sync(kAdslSyncWord))};

// What the detector consumed is gone from the report, so the frame arrives
// behind the rest of whichever sync word it was.
constexpr uint8_t kSyncTailBits = static_cast<uint8_t>(32u - kSharedSyncEndChip / 2u);
constexpr uint8_t kSyncTailBytes = 3;

constexpr uint32_t sync_tail(uint32_t sync_word) {
    return (sync_word & ((1u << kSyncTailBits) - 1u)) << (8u * kSyncTailBytes - kSyncTailBits);
}

// Read the sync tail plus the longer of the two frames, and read it as chips.
constexpr uint8_t kRxFrameBytes = kSyncTailBytes + kAlptasFrameBytes;
constexpr uint8_t kRxChipBytes = 2 * kRxFrameBytes;
constexpr uint8_t kSyncChipBytes = 8;
constexpr uint8_t kTxChipBytes = kSyncChipBytes + 2 * kAlptasFrameBytes;

struct Frame {
    System system{System::Unknown};
    uint8_t len{0};
    uint16_t bad_chips{0};
    uint8_t data[kRxFrameBytes]{};
    uint8_t err[kRxFrameBytes]{};
};

uint8_t frame_bytes(System system);

// The chip stream a transmitter puts on air: the whole sync word, then the
// frame, both Manchester-encoded.
size_t encode_mband(uint32_t sync_word, const uint8_t* frame, uint8_t frame_len, uint8_t* chips);

// The chips a receiver reports once the shared window matched. Manchester-decode
// them, name the system by the sync tail, and shift the frame to byte zero.
bool receive_mband(const uint8_t* chips, size_t chip_bytes, Frame& out);

}  // namespace skyblip::protocol

#endif
