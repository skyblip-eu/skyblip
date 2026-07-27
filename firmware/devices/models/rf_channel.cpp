#include "devices/models/rf_channel.h"

namespace skyblip::models {

int RfChannel::apply_ber(uint8_t* data, size_t len, double ber, uint8_t* err) {
    int flips = 0;
    for (size_t i = 0; i < len; i++) {
        for (int b = 0; b < 8; b++) {
            if (uniform() < ber) {
                uint8_t mask = static_cast<uint8_t>(0x80 >> b);
                data[i] ^= mask;
                if (err) err[i] |= mask;
                flips++;
            }
        }
    }
    return flips;
}

int RfChannel::apply_burst(uint8_t* data, size_t len, int burst_bits, uint8_t* err) {
    size_t total_bits = len * 8;
    if (total_bits == 0) return 0;
    size_t start = next() % total_bits;
    int flips = 0;
    for (int k = 0; k < burst_bits; k++) {
        size_t bit = (start + static_cast<size_t>(k)) % total_bits;
        size_t byte = bit >> 3;
        uint8_t mask = static_cast<uint8_t>(0x80 >> (bit & 7));
        data[byte] ^= mask;
        if (err) err[byte] |= mask;
        flips++;
    }
    return flips;
}

int RfChannel::apply_symbol_errors(uint8_t* data, size_t len, int n) {
    if (len == 0) return 0;
    int applied = 0;
    for (int i = 0; i < n; i++) {
        size_t pos = next() % len;
        uint8_t v;
        do {
            v = static_cast<uint8_t>(next());
        } while (v == 0);
        data[pos] ^= v;
        applied++;
    }
    return applied;
}

}
