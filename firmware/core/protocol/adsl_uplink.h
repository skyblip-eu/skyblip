// core/protocol/adsl_uplink.h: ADS-L Traffic Uplink (O-band HDR, 869.525 GMSK)
#ifndef SKYBLIP_CORE_PROTOCOL_ADSL_UPLINK_H
#define SKYBLIP_CORE_PROTOCOL_ADSL_UPLINK_H

#include <cstddef>
#include <cstdint>

#include "core/fec/reed_solomon.h"
#include "core/messages/messages.h"
#include "core/util/result.h"

namespace skyblip::protocol {

// §C.4: the O-band HDR carrier is not Manchester-coded, so the detector matches
// the sync word itself. §C.4.3 gives two bytes, 0x2D 0xD4, and §D.1.1 puts the
// Packet Length field immediately behind them: the length of the message
// excluding the length byte itself, which for this frame is the whole RS
// codeword. Arming on all three costs nothing and rejects a burst of any other
// length before the decoder ever sees it.
static_assert(fec::ReedSolomon255::kN <= 255, "the §D.1.1 length field is one byte wide");
static_assert(fec::ReedSolomon255::kN <= messages::kRfEventBytes,
              "an uplink frame would be truncated on its way off the radio");
constexpr uint8_t kUplinkFrameBytes = static_cast<uint8_t>(fec::ReedSolomon255::kN);
constexpr uint8_t kUplinkSync[3] = {0x2D, 0xD4, kUplinkFrameBytes};
constexpr uint8_t kUplinkSyncBits = 24;
constexpr int kUplinkBurstBytes = static_cast<int>(sizeof(kUplinkSync)) + kUplinkFrameBytes;

// §C.4's modulation table, which is not §C.2's: the O band runs at twice the
// chip rate, is Gaussian-shaped, and needs a wider receiver. A dwell that
// retunes the synthesiser and nothing else listens to this band at half its
// rate, which is deaf.
constexpr uint32_t kUplinkChipRateBps = 200000;
constexpr uint32_t kUplinkDeviationHz = 50000;
constexpr uint32_t kUplinkChannelBandwidthHz = 250000;
constexpr uint16_t kUplinkGaussianBtE2 = 50;

// §C.4.2: the preamble is "10" twelve times, ahead of the sync word.
constexpr uint16_t kUplinkPreambleChips = 24;

// How long one uplink burst occupies the channel: preamble, sync word, length
// byte and codeword, at §C.4's chip rate. The dwell map (core/timing/slot.h) is
// cut against this, so it is derived rather than remembered.
constexpr uint32_t kUplinkBurstBits =
    kUplinkPreambleChips + kUplinkSyncBits + 8u * kUplinkFrameBytes;
constexpr uint32_t kUplinkBurstUs = kUplinkBurstBits * 1000000u / kUplinkChipRateBps;

class AdslUplink {
   public:
    static constexpr int kVersion = 1;
    static constexpr int kRecordBytes = 16;
    static constexpr int kHeaderBytes = 3;
    static constexpr int kMaxTargets = (fec::ReedSolomon255::kK - kHeaderBytes) / kRecordBytes;
    static constexpr int kFrameBytes = fec::ReedSolomon255::kN;

    struct DecodeStats {
        int targets{0};
        int corrected{0};
        // Records the frame carried and the decoder refused. Reed-Solomon can
        // correct to a valid codeword that is not the one that was sent, so a
        // frame that passes parity can still hold nonsense: what it holds is
        // checked before any of it becomes traffic.
        int rejected{0};
    };

    AdslUplink() = default;

    Status encode(const messages::AircraftObs* targets, int n, uint8_t key_index,
                  uint8_t out_frame[kFrameBytes]) const;

    Status decode(const uint8_t frame[kFrameBytes], messages::AircraftObs* targets, int cap,
                  DecodeStats& stats) const;

   private:
    fec::ReedSolomon255 rs_;
};

}

#endif
