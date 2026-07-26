// core/fec/manchester.h — Manchester line coding (M-band only, ADS-L §C.2.1).
#ifndef SKYBLIP_CORE_FEC_MANCHESTER_H
#define SKYBLIP_CORE_FEC_MANCHESTER_H

#include <cstddef>
#include <cstdint>

namespace skyblip::fec {

uint8_t manchester_encode_nibble(uint8_t nibble);

uint8_t manchester_decode_byte(uint8_t coded);

void manchester_encode(const uint8_t* data, size_t len, uint8_t* out);

size_t manchester_decode(const uint8_t* coded, size_t data_len, uint8_t* data, uint8_t* err);

}

#endif
