#include "core/settings/address.h"

namespace skyblip::settings {

namespace {
constexpr uint8_t kFlarmPrefixes[] = {0xD0, 0xDD, 0xDE, 0xDF};
constexpr uint8_t kSkytraxxPrefix = 0x11;

uint8_t prefix_of(uint32_t addr) { return static_cast<uint8_t>((addr >> 16) & 0xFF); }
}  // namespace

bool address_is_crowded(uint32_t addr) {
    const uint8_t prefix = prefix_of(addr & kAddressMask);
    for (uint8_t p : kFlarmPrefixes)
        if (prefix == p) return true;
    return prefix == kSkytraxxPrefix;
}

// Our address travels with an ADS-L address table that says which space it was
// drawn from, so a prefix collision is not a protocol ambiguity for us. It is
// still a collision for everything downstream that flattens the three systems
// into one 24-bit space: our own $PFLAA output prints the address with an
// id-type of 0 for anything that is not ICAO or FLARM, and an EFB merges two
// aircraft that share an address into one symbol. The prefixes below are the
// ones populated by trackers that mint their identity the same way we do.
//
// Not dodged: 0x5B. SoftRF avoids it because OGN 0.2.8+ misdecodes 'Air V6'
// traffic carrying it, and Air V6 is the legacy FLARM frame, which this
// firmware never transmits.
uint32_t safe_device_address(uint32_t chip_id) {
    uint32_t addr = chip_id & kAddressMask;
    const uint8_t prefix = prefix_of(addr);

    for (uint8_t p : kFlarmPrefixes)
        if (prefix == p) addr = (addr + kFlarmRangeOffset) & kAddressMask;
    if (prefix == kSkytraxxPrefix) addr = (addr + kVendorRangeOffset) & kAddressMask;

    if (addr == kUnusableLow || addr == kUnusableHigh) return kFallbackAddress;
    return addr;
}

uint32_t safe_air_address(uint32_t addr, uint8_t addr_table) {
    if (addr_table > kAddrTableSelfMintedMax) return addr & kAddressMask;
    return safe_device_address(addr);
}

}  // namespace skyblip::settings
