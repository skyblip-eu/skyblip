#include "core/fec/reed_solomon.h"

namespace skyblip::fec {

ReedSolomon255::ReedSolomon255() {
    uint16_t x = 1;
    for (int i = 0; i < 255; i++) {
        exp_[i] = static_cast<uint8_t>(x);
        log_[x] = static_cast<uint8_t>(i);
        x <<= 1;
        if (x & 0x100) x ^= 0x11D;
    }
    for (int i = 255; i < 512; i++) exp_[i] = exp_[i - 255];
    log_[0] = 0;

    gen_[0] = 1;
    for (int i = 0; i < kParity; i++) {
        gen_[i + 1] = 0;
        uint8_t root = exp_[i];
        for (int j = i + 1; j > 0; j--) {
            gen_[j] = static_cast<uint8_t>(gen_[j - 1] ^ mul(gen_[j], root));
        }
        gen_[0] = mul(gen_[0], root);
    }
}

void ReedSolomon255::encode(const uint8_t data[kK], uint8_t parity[kParity]) const {
    for (int i = 0; i < kParity; i++) parity[i] = 0;
    for (int i = 0; i < kK; i++) {
        uint8_t feedback = static_cast<uint8_t>(data[i] ^ parity[0]);
        for (int j = 0; j < kParity - 1; j++) {
            parity[j] = static_cast<uint8_t>(parity[j + 1] ^ mul(feedback, gen_[kParity - 1 - j]));
        }
        parity[kParity - 1] = mul(feedback, gen_[0]);
    }
}

void ReedSolomon255::calc_syndromes(const uint8_t* cw, uint8_t* synd, bool& all_zero) const {
    all_zero = true;
    for (int i = 0; i < kParity; i++) {
        uint8_t s = 0;
        uint8_t root = exp_[i];
        for (int j = 0; j < kN; j++) s = static_cast<uint8_t>(cw[j] ^ mul(s, root));
        synd[i] = s;
        if (s) all_zero = false;
    }
}

bool ReedSolomon255::syndromes_zero(const uint8_t cw[kN]) const {
    uint8_t synd[kParity];
    bool z;
    calc_syndromes(cw, synd, z);
    return z;
}

int ReedSolomon255::decode(uint8_t cw[kN]) const {
    uint8_t synd[kParity];
    bool all_zero;
    calc_syndromes(cw, synd, all_zero);
    if (all_zero) return 0;

    uint8_t lambda[kParity + 1] = {1};
    uint8_t prev[kParity + 1] = {1};
    uint8_t tmp[kParity + 1];
    int l = 0;
    int m = 1;
    uint8_t b = 1;
    for (int n = 0; n < kParity; n++) {
        uint8_t delta = synd[n];
        for (int i = 1; i <= l; i++) delta ^= mul(lambda[i], synd[n - i]);
        if (delta == 0) {
            m++;
        } else if (2 * l <= n) {
            for (int i = 0; i <= kParity; i++) tmp[i] = lambda[i];
            uint8_t coef = mul(delta, inv(b));
            for (int i = 0; i + m <= kParity; i++)
                lambda[i + m] = static_cast<uint8_t>(lambda[i + m] ^ mul(coef, prev[i]));
            l = n + 1 - l;
            for (int i = 0; i <= kParity; i++) prev[i] = tmp[i];
            b = delta;
            m = 1;
        } else {
            uint8_t coef = mul(delta, inv(b));
            for (int i = 0; i + m <= kParity; i++)
                lambda[i + m] = static_cast<uint8_t>(lambda[i + m] ^ mul(coef, prev[i]));
            m++;
        }
    }

    int nerr = 0;
    for (int i = kParity; i >= 0; i--) {
        if (lambda[i]) {
            nerr = i;
            break;
        }
    }
    if (nerr == 0 || nerr > kMaxErrors) return -1;

    int eround_robin_pos[kMaxErrors];
    int found = 0;
    for (int i = 0; i < kN; i++) {
        uint8_t sum = 1;
        uint8_t xinv = exp_[(255 - i) % 255];
        uint8_t xp = 1;
        for (int j = 1; j <= nerr; j++) {
            xp = mul(xp, xinv);
            sum ^= mul(lambda[j], xp);
        }
        if (sum == 0) {
            if (found >= kMaxErrors) return -1;
            eround_robin_pos[found++] = i;
        }
    }
    if (found != nerr) return -1;

    uint8_t omega[kParity + 1] = {0};
    for (int i = 0; i < kParity; i++) {
        uint8_t s = synd[i];
        for (int j = 0; j <= i; j++) omega[i] ^= mul(lambda[j], synd[i - j]);
        (void)s;
    }
    for (int k = 0; k < found; k++) {
        int pos = eround_robin_pos[k];
        uint8_t xinv = exp_[(255 - pos) % 255];
        uint8_t omega_v = 0;
        uint8_t xp = 1;
        for (int j = 0; j < kParity; j++) {
            omega_v ^= mul(omega[j], xp);
            xp = mul(xp, xinv);
        }
        uint8_t lambda_der = 0;
        uint8_t xq = 1;
        for (int j = 1; j <= nerr; j += 2) {
            lambda_der ^= mul(lambda[j], xq);
            xq = mul(xq, mul(xinv, xinv));
        }
        if (lambda_der == 0) return -1;
        uint8_t xk = exp_[pos];
        uint8_t mag = mul(xk, mul(omega_v, inv(lambda_der)));
        int idx = kN - 1 - pos;
        if (idx < 0 || idx >= kN) return -1;
        cw[idx] ^= mag;
    }

    if (!syndromes_zero(cw)) return -1;
    return found;
}

}
