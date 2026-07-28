// ADS-L data scrambling (ADS-L 4 SRD-860 issue 2 §E.2). The Key-0 XXTEA variant
#ifndef SKYBLIP_CORE_FEC_SCRAMBLE_H
#define SKYBLIP_CORE_FEC_SCRAMBLE_H

#include <cstdint>

namespace skyblip::fec {

void xxtea_scramble_key0(uint32_t* data, uint8_t words, uint8_t loops);
void xxtea_descramble_key0(uint32_t* data, uint8_t words, uint8_t loops);

}

#endif
