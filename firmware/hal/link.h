// hal/link.h: capability port: OUTBOUND bytes to a companion link (BLE/USB/TCP)
#ifndef SKYBLIP_HAL_LINK_H
#define SKYBLIP_HAL_LINK_H

#include "core/messages/messages.h"
#include "core/util/result.h"
#include "core/util/span.h"

namespace skyblip::hal {

class Link {
   public:
    virtual ~Link() = default;
    virtual Status send(messages::Endpoint ep, ConstByteSpan bytes) = 0;
};

}

#endif
