#ifndef SKYBLIP_HAL_ROLES_H
#define SKYBLIP_HAL_ROLES_H

#include "hal/annunciator.h"
#include "hal/capabilities.h"
#include "hal/clock.h"
#include "hal/dfu.h"
#include "hal/display.h"
#include "hal/kvstore.h"
#include "hal/link.h"
#include "hal/rf.h"

namespace skyblip::hal {

// Every role is a reference: an absent capability is filled by its null part, so
// no caller branches on a pointer. What is absent is stated once, in capabilities.
struct Roles {
    Clock& clock;
    Rf& rf;
    Link& link;
    Display& display;
    KvStore& kv;
    Annunciator& annunciator;
    Dfu& dfu;
    Capabilities capabilities{Capability::None};
    uint32_t device_addr{0};
};

}  // namespace skyblip::hal

#endif
