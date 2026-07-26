// core/fec/crc.h — checksum / CRC primitives.
#ifndef SKYBLIP_CORE_FEC_CRC_H
#define SKYBLIP_CORE_FEC_CRC_H

#include <cstddef>
#include <cstdint>

namespace skyblip::fec {

uint32_t adsl_pi_pass(uint32_t crc, uint8_t byte);

uint32_t adsl_pi_check(const uint8_t* data, size_t len);

uint32_t adsl_pi_calc(const uint8_t* data, size_t len);

uint16_t crc16_ccitt(const uint8_t* data, size_t len, uint16_t init = 0);

uint32_t crc32(const uint8_t* data, size_t len, uint32_t init = 0xFFFFFFFFu);

}

#endif
