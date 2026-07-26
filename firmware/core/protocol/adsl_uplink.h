// core/protocol/adsl_uplink.h — ADS-L Traffic Uplink (O-band HDR, 869.525 GMSK
#ifndef SKYBLIP_CORE_PROTOCOL_ADSL_UPLINK_H
#define SKYBLIP_CORE_PROTOCOL_ADSL_UPLINK_H

#include <cstddef>
#include <cstdint>

#include "core/fec/reed_solomon.h"
#include "core/messages/messages.h"
#include "core/util/result.h"

namespace skyblip::protocol {

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
