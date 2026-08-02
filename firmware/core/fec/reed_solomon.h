// core/fec/reed_solomon.h: Reed-Solomon RS(255,223) over GF(2^8).
#ifndef SKYBLIP_CORE_FEC_REED_SOLOMON_H
#define SKYBLIP_CORE_FEC_REED_SOLOMON_H

#include <cstddef>
#include <cstdint>

namespace skyblip::fec {

class ReedSolomon255 {
   public:
    static constexpr int kN = 255;
    static constexpr int kK = 223;
    static constexpr int kParity = kN - kK;
    static constexpr int kMaxErrors = kParity / 2;

    ReedSolomon255();

    void encode(const uint8_t data[kK], uint8_t parity[kParity]) const;

    int decode(uint8_t codeword[kN]) const;

    bool syndromes_zero(const uint8_t codeword[kN]) const;

   private:
    uint8_t exp_[512];
    uint8_t log_[256];
    uint8_t gen_[kParity + 1];

    uint8_t mul(uint8_t a, uint8_t b) const {
        if (a == 0 || b == 0) return 0;
        return exp_[log_[a] + log_[b]];
    }
    uint8_t inv(uint8_t a) const { return exp_[255 - log_[a]]; }
    void calc_syndromes(const uint8_t* cw, uint8_t* synd, bool& all_zero) const;
};

}

#endif
