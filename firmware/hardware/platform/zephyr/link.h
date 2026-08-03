#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_LINK_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_LINK_H
#if defined(__ZEPHYR__)

#include "core/messages/messages.h"
#include "hal/link.h"

namespace skyblip::platform::zephyr {

class Link : public hal::Link {
   public:
    Status begin();  // bt_enable() + start connectable advertising

    // INFO: fc 04aug26 Read from the connection every time rather than latched
    // at connect: bt_gatt_get_mtu() already tracks the exchange, so a central
    // that negotiates late (iOS exchanges after it has discovered the service)
    // cannot leave a stale figure behind. No exchange is requested from this
    // side - see link.cpp.
    uint16_t payload_bytes() const override;
    Status send(messages::Endpoint ep, ConstByteSpan bytes) override;

    // Non-blocking: pop one queued inbound frame (config writes). The shell
    // drains this into App::on_link_rx(). Returns false when empty.
    bool pop_rx(messages::RxFrame& out);
};

Link& link();

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
