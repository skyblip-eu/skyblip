// hal/link.h: capability port: OUTBOUND bytes to a companion link (BLE/USB/TCP)
#ifndef SKYBLIP_HAL_LINK_H
#define SKYBLIP_HAL_LINK_H

#include "core/messages/messages.h"
#include "core/util/result.h"
#include "core/util/span.h"

namespace skyblip::hal {

// INFO: fc 04aug26 What BLE guarantees and not one byte more: the 23-byte
// default ATT_MTU less the three bytes of notification header (opcode + value
// handle). Both upstream SoftRF forks resolve the whole MTU question by never
// sending more than this - BLE_MAX_WRITE_CHUNK_SIZE is 20 in every one of their
// BLE NUS paths, and their `setMTU` call is commented out with the note "Apple
// needs 23 apparently". We do better than 20 when the central agrees to, which
// is the only reason payload_bytes() exists.
constexpr uint16_t kMinimumLinkPayload = 20;

class Link {
   public:
    virtual ~Link() = default;

    // INFO: fc 04aug26 What one frame may carry, asked at the moment a frame is
    // formatted rather than remembered from a link-up event: the MTU is
    // negotiated after the connection is up, and a central may exchange it late.
    // An iOS central commonly settles at ATT_MTU 185, which is 182 bytes here.
    // A link that has never been told anything answers the guaranteed minimum,
    // so a port that forgets to override this under-promises instead of lying.
    virtual uint16_t payload_bytes() const { return kMinimumLinkPayload; }

    // INFO: fc 04aug26 Longer than payload_bytes() is refused, never truncated:
    // a controller does not shorten an oversized notification, it fails it, and
    // a caller that learns nothing about that has silently dropped the frame.
    virtual Status send(messages::Endpoint ep, ConstByteSpan bytes) = 0;
};

}

#endif
