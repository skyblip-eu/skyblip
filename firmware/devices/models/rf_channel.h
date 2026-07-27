// devices/models/rf_channel.h — the RF channel reduced to an injectable scalar:
// a bit error rate, a burst length, or a symbol-error count applied to a frame.
#ifndef SKYBLIP_DEVICES_MODELS_RF_CHANNEL_H
#define SKYBLIP_DEVICES_MODELS_RF_CHANNEL_H

#include <cstddef>
#include <cstdint>

namespace skyblip::models {

class RfChannel {
   public:
    explicit RfChannel(uint32_t seed = 1) : seed_(seed) {}

    int apply_ber(uint8_t* data, size_t len, double ber, uint8_t* err = nullptr);

    int apply_burst(uint8_t* data, size_t len, int burst_bits, uint8_t* err = nullptr);

    int apply_symbol_errors(uint8_t* data, size_t len, int n);

    uint32_t next() {
        seed_ ^= seed_ << 13;
        seed_ ^= seed_ >> 17;
        seed_ ^= seed_ << 5;
        return seed_;
    }
    double uniform() { return (next() >> 8) * (1.0 / 16777216.0); }

   private:
    uint32_t seed_;
};

}

#endif
