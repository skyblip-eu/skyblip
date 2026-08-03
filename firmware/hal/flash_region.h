// hal/flash_region.h: capability port: a contiguous region of NOR flash, with
// its erase granularity exposed rather than hidden.
//
// hal/kvstore.h is the other storage port and it is not this one. A key/value
// store owns its own layout, garbage-collects when it feels like it, and is
// mounted on the 32 KB internal partition where a write stalls the CPU. A flight
// log is an append-only stream on a 1.29 MB external partition where a write
// does not. The two have nothing in common but the word storage, so they are two
// ports and not one with a mode flag.
#ifndef SKYBLIP_HAL_FLASH_REGION_H
#define SKYBLIP_HAL_FLASH_REGION_H

#include <cstdint>

#include "core/util/result.h"

namespace skyblip::hal {

class FlashRegion {
   public:
    virtual ~FlashRegion() = default;

    // False when the part did not answer its probe. Capability::Storage says the
    // device is meant to have durable storage; this says this region of it came
    // up. Absent either way means the device flies and logs nothing.
    virtual bool ready() const = 0;

    virtual uint32_t sector_bytes() const = 0;
    virtual uint32_t sector_count() const = 0;

    virtual Status read(uint32_t offset, uint8_t* buf, uint32_t len) = 0;
    // NOR semantics, stated because callers depend on them: a program can only
    // clear bits, so the target must have been erased and no byte may be written
    // twice.
    virtual Status write(uint32_t offset, const uint8_t* buf, uint32_t len) = 0;
    virtual Status erase_sector(uint32_t index) = 0;
};

}  // namespace skyblip::hal

#endif
