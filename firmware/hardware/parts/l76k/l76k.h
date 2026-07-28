// A GNSS receiver has no registers to drive: it produces. This driver owns the
// UART and the NMEA parser and hands out a fix, which the board pushes onto the bus.
#ifndef SKYBLIP_HARDWARE_PARTS_L76K_H
#define SKYBLIP_HARDWARE_PARTS_L76K_H

#include "core/gnss/nmea.h"
#include "hardware/io/io.h"

namespace skyblip::parts {

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

}  // namespace skyblip::parts

#endif
