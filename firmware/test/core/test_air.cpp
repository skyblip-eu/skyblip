// The shared sync window: one M-band dwell, both systems. If any of these fail,
// the receiver is deaf to half the sky and does not know it.
#include <cstdint>
#include <cstring>

#include "core/fec/manchester.h"
#include "core/protocol/air.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {

// A burst as it leaves a transmitter, then as the radio reports it: the detector
// matched 16 chips two chips into the sync word, so everything before the match
// is gone and the report is byte-aligned on what follows.
void deliver(const uint8_t* chips, size_t chip_len, uint8_t* out, size_t out_len) {
    const size_t first_bit = protocol::kSharedSyncSkipChips + protocol::kSharedSyncBits;
    for (size_t i = 0; i < out_len; i++) {
        uint8_t byte = 0;
        for (int b = 0; b < 8; b++) {
            const size_t bit = first_bit + i * 8 + static_cast<size_t>(b);
            const bool set = bit < chip_len * 8 && ((chips[bit >> 3] >> (7 - (bit & 7))) & 1u);
            byte = static_cast<uint8_t>((byte << 1) | (set ? 1u : 0u));
        }
        out[i] = byte;
    }
}

void frame_of(uint32_t sync_word, const uint8_t* payload, uint8_t payload_len,
              protocol::Frame& out) {
    uint8_t chips[protocol::kTxChipBytes] = {0};
    const size_t chip_len = protocol::encode_mband(sync_word, payload, payload_len, chips);
    uint8_t reported[protocol::kRxChipBytes] = {0};
    deliver(chips, chip_len, reported, sizeof(reported));
    REQUIRE(protocol::receive_mband(reported, sizeof(reported), out));
}

void fill(uint8_t* bytes, uint8_t len, uint8_t seed) {
    for (uint8_t i = 0; i < len; i++) bytes[i] = static_cast<uint8_t>(i * 31 + seed);
}

}  // namespace

TEST_CASE("air: the constexpr chip coding is the one fec::manchester_encode uses") {
    for (int byte = 0; byte < 256; byte++) {
        uint8_t coded[2];
        const uint8_t in = static_cast<uint8_t>(byte);
        fec::manchester_encode(&in, 1, coded);
        const uint16_t chips = protocol::manchester_chips(in);
        CHECK(coded[0] == static_cast<uint8_t>(chips >> 8));
        CHECK(coded[1] == static_cast<uint8_t>(chips));
    }
}

// The trick: the two sync words are different, their chips agree for 18, and the
// 16 the detector is armed with are the byte-aligned middle of that agreement.
TEST_CASE("air: the two sync words share the window the detector matches") {
    CHECK(protocol::kAdslSyncWord != protocol::kAlptasSyncWord);
    CHECK(protocol::shared_sync(protocol::kAdslSyncWord) ==
          protocol::shared_sync(protocol::kAlptasSyncWord));
    CHECK(protocol::kSharedSync[0] == 0x56);
    CHECK(protocol::kSharedSync[1] == 0x66);

    // 23 bits of sync word are still ahead of the frame when the report starts,
    // and they are what names the system.
    CHECK(protocol::kSyncTailBits == 23);
    CHECK(protocol::sync_tail(protocol::kAdslSyncWord) == 0xE49630u);
    CHECK(protocol::sync_tail(protocol::kAlptasSyncWord) == 0x63F56Cu);
}

TEST_CASE("air: an ADS-L burst frames as ADS-L, byte-aligned on its data") {
    uint8_t payload[protocol::kAdslFrameBytes];
    fill(payload, sizeof(payload), 7);

    protocol::Frame frame{};
    frame_of(protocol::kAdslSyncWord, payload, sizeof(payload), frame);
    CHECK(frame.system == protocol::System::AdslDirect);
    CHECK(frame.len == protocol::kAdslFrameBytes);
    CHECK(std::memcmp(frame.data, payload, sizeof(payload)) == 0);
    CHECK(frame.bad_chips == 0);
}

TEST_CASE("air: an ALP-TAS burst frames as ALP-TAS through the same window") {
    uint8_t payload[protocol::kAlptasFrameBytes];
    fill(payload, sizeof(payload), 19);

    protocol::Frame frame{};
    frame_of(protocol::kAlptasSyncWord, payload, sizeof(payload), frame);
    CHECK(frame.system == protocol::System::Alptas);
    CHECK(frame.len == protocol::kAlptasFrameBytes);
    CHECK(std::memcmp(frame.data, payload, sizeof(payload)) == 0);
}

// A dead chip pair in the sync tail must not cost the frame: it is 23 bits long
// and the two systems differ in far more than one of them.
TEST_CASE("air: one flipped chip in the sync tail still names the system") {
    uint8_t payload[protocol::kAdslFrameBytes];
    fill(payload, sizeof(payload), 3);
    uint8_t chips[protocol::kTxChipBytes] = {0};
    const size_t chip_len =
        protocol::encode_mband(protocol::kAdslSyncWord, payload, sizeof(payload), chips);
    chips[5] ^= 0x40;  // inside the sync word, past the matched window

    uint8_t reported[protocol::kRxChipBytes] = {0};
    deliver(chips, chip_len, reported, sizeof(reported));
    protocol::Frame frame{};
    REQUIRE(protocol::receive_mband(reported, sizeof(reported), frame));
    CHECK(frame.system == protocol::System::AdslDirect);
    CHECK(std::memcmp(frame.data, payload, sizeof(payload)) == 0);
}

TEST_CASE("air: a chip error inside the frame is reported, not hidden") {
    uint8_t payload[protocol::kAdslFrameBytes];
    fill(payload, sizeof(payload), 11);
    uint8_t chips[protocol::kTxChipBytes] = {0};
    const size_t chip_len =
        protocol::encode_mband(protocol::kAdslSyncWord, payload, sizeof(payload), chips);
    chips[20] ^= 0x01;  // one chip of a pair: no transition left, so no bit either

    uint8_t reported[protocol::kRxChipBytes] = {0};
    deliver(chips, chip_len, reported, sizeof(reported));
    protocol::Frame frame{};
    REQUIRE(protocol::receive_mband(reported, sizeof(reported), frame));
    CHECK(frame.system == protocol::System::AdslDirect);
    CHECK(frame.bad_chips > 0);
    bool flagged = false;
    for (uint8_t i = 0; i < frame.len; i++)
        if (frame.err[i] != 0) flagged = true;
    CHECK(flagged);
}

TEST_CASE("air: a foreign sync word frames as nothing") {
    uint8_t payload[protocol::kAdslFrameBytes];
    fill(payload, sizeof(payload), 5);
    uint8_t chips[protocol::kTxChipBytes] = {0};
    // Same leading byte, so the shared window still matches, then a tail neither
    // system uses: OGN's own sync word past its first byte.
    const size_t chip_len = protocol::encode_mband(0xF5F3656Cu, payload, sizeof(payload), chips);

    uint8_t reported[protocol::kRxChipBytes] = {0};
    deliver(chips, chip_len, reported, sizeof(reported));
    protocol::Frame frame{};
    CHECK_FALSE(protocol::receive_mband(reported, sizeof(reported), frame));
    CHECK(frame.system == protocol::System::Unknown);
    CHECK(frame.len == 0);
}

TEST_CASE("air: a report shorter than the dwell reads is refused") {
    uint8_t reported[protocol::kRxChipBytes] = {0};
    protocol::Frame frame{};
    CHECK_FALSE(protocol::receive_mband(reported, protocol::kRxChipBytes - 1, frame));
}
