// core/settings/address.h: the 24-bit identity this device claims on the air.
// The chip id is a serial number, not an address: some of the space it can land
// in is already crowded by other trackers that derive their identity the same
// way. This is the one place that decides where we may not sit.
#ifndef SKYBLIP_CORE_SETTINGS_ADDRESS_H
#define SKYBLIP_CORE_SETTINGS_ADDRESS_H

#include <cstdint>

namespace skyblip::settings {

constexpr uint32_t kAddressMask = 0x00FFFFFFu;

// 0x000000 is "no address" to every decoder that reads one, and 0xFFFFFF is the
// all-ones pattern a dead SPI read produces. Neither may go on the air, so a
// chip id that lands on one is answered with a fixed address instead.
constexpr uint32_t kUnusableLow = 0x000000u;
constexpr uint32_t kUnusableHigh = kAddressMask;
constexpr uint32_t kFallbackAddress = 0x7E5701u;

// INFO: hk 02aug26 the leading byte of the 24-bit id, not the ADS-L address
// table. SoftRF moves off 0xD0/0xDD/0xDE/0xDF (congested FLARM range) and 0x11
// (Skytraxx) with these exact offsets
// (oss/SoftRF-lyusupov .../src/system/SoC.cpp:83-110). Both offsets land outside
// every dodged prefix, which is what makes the mapping idempotent.
constexpr uint32_t kFlarmRangeOffset = 0x100000u;
constexpr uint32_t kVendorRangeOffset = 0x010000u;

// ADS-L 4 SRD860 issue 2 address table: 0 to 4 are random/anonymous addresses a
// device mints for itself, 5 is ICAO, 6 FLARM, 7 OGN. Only the first group is
// ours to move; an address that was issued to the aircraft goes on the air as
// issued, whatever prefix it carries.
constexpr uint8_t kAddrTableSelfMintedMax = 4;

bool address_is_crowded(uint32_t addr);

uint32_t safe_device_address(uint32_t chip_id);

uint32_t safe_air_address(uint32_t addr, uint8_t addr_table);

}  // namespace skyblip::settings

#endif
