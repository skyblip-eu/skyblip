#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_LINK_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_LINK_H
#if defined(__ZEPHYR__)

#include "core/messages/messages.h"
#include "hal/link.h"

namespace skyblip::platform::zephyr {

class Link : public hal::Link {
   public:
    Status begin();  // bt_enable() + start connectable advertising
    Status send(messages::Endpoint ep, ConstByteSpan bytes) override;

    // Non-blocking: pop one queued inbound frame (config writes). The shell
    // drains this into App::on_link_rx(). Returns false when empty.
    bool pop_rx(messages::RxFrame& out);
};

Link& link();

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
