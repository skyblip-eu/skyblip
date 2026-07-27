// devices/drivers/l76k.h — the L76K GNSS receiver (GPS/GLONASS/BeiDou/QZSS).
//
// A GNSS receiver has no registers to drive: it PRODUCES. So this driver is the
// producer side of the hal (3-ARCHITECTURE §8 "who-initiates") — it owns the
// UART and the NMEA parser, and hands out a fix. It does NOT belong in
// products::Ports: a shell polls it and pushes each new fix into App, exactly as
// it drains the BLE link's rx fifo.
//
//   if (gnss.poll()) app.on_gnss_fix(gnss.fix());
//
// Depends on devices/io only, so it is MCU-agnostic and runs on the host against
// devices/models/l76k.h.
#ifndef SKYBLIP_DEVICES_DRIVERS_L76K_H
#define SKYBLIP_DEVICES_DRIVERS_L76K_H

#include "core/gnss/nmea.h"
#include "devices/io/io.h"

namespace skyblip::drivers {

class L76k {
   public:
    explicit L76k(io::Uart& uart) : uart_(uart) {}

    // Drain whatever the receiver has said into the parser. Returns true when
    // that produced a NEW fix, so the caller never re-applies a stale one.
    bool poll();

    const gnss::GnssFix& fix() const { return parser_.fix(); }

    // Sentences the parser accepted since boot. A receiver that is wired but
    // silent (or babbling at the wrong baud) never moves this off zero, which is
    // what the DFU health gate watches.
    uint32_t updates() const { return parser_.fix().updates; }

   private:
    static constexpr size_t kChunk = 64;

    io::Uart& uart_;
    gnss::NmeaParser parser_{};
    uint32_t applied_{0};
};

}  // namespace skyblip::drivers

#endif
