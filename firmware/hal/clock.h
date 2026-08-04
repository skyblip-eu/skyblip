// hal/clock.h: capability port: a monotonic clock core/ can read.
//
// millis() WRAPS. It is a 32-bit millisecond counter, so it counts to 49.7 days
// and then continues from zero, and this device is expected to fly through that
// instant rather than reboot around it the way SoftRF does
// (oss/SoftRF-lyusupov/.../src/platform/nRF52.h:270-276: a ground station may
// reboot itself at 46 days, an aircraft may not). One rule holds everywhere a
// deadline is held, and it is the whole rule:
//
//  1. Measure an interval as an unsigned difference, `now - then`, and compare
//     THAT against a duration. The subtraction is exact across the wrap, because
//     it is modulo 2^32, for any interval shorter than 49.7 days.
//  2. Never compare two instants. `now < until` and `now >= deadline` are the
//     bug: one wrapped operand inverts the answer, and what came out of the one
//     the tree had was a transmitter silent for seven weeks instead of two
//     seconds (core/timing/transmit.cpp).
//  3. Never take the difference into a signed type first. `int32_t(now - then)`
//     is a wider trap than the one it looks like it closes.
//  4. No instant is a sentinel. The counter passes through 0 at every wrap, so a
//     stamp of 0 meaning "never" is a state the clock itself produces. Keep a
//     bool beside the stamp.
//
// Nothing here needs an interval longer than 49.7 days, so no 64-bit accumulator
// is maintained from this tick: the widest deadline in the tree is the rolling
// duty-cycle hour (core/timing/channel.h) and the rest are seconds. micros() is
// already 64-bit and does not wrap in any life this device has, which is why
// hal/rf.h arms slot deadlines on it and slot timing is not exposed to any of
// the above.
//
// An uptime a person reads is the same rule's last line: now_ms / 1000 counts 49
// days and then starts again at zero, which is honest and says so. A negative age
// or a four-billion-millisecond gap is not, and it is what breaking rules 2 and 4
// produces.
#ifndef SKYBLIP_HAL_CLOCK_H
#define SKYBLIP_HAL_CLOCK_H

#include <cstdint>

namespace skyblip::hal {

class Clock {
   public:
    virtual ~Clock() = default;
    virtual uint32_t millis() const = 0;
    virtual uint64_t micros() const = 0;
};

}

#endif
