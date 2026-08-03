// What the channel is worth before we speak on it, and what we have already
// spent on it: ADS-L 4 SRD-860 issue 2 §D.3 (CSMA, listen before talk) and
// EN 300 220-2 V3.3.1 Table 4 band M (868.0-868.6 MHz, 25 mW e.r.p., 1% duty
// cycle), which is the channel-access route this product declares.
#ifndef SKYBLIP_CORE_TIMING_CHANNEL_H
#define SKYBLIP_CORE_TIMING_CHANNEL_H

#include <cstdint>

namespace skyblip::timing {

// The level an empty channel sits at, here and now. A fixed carrier-sense
// threshold is a device that goes silent at a noisy site and never says why, so
// the threshold is a measurement plus a margin instead of a constant.
class NoiseFloor {
   public:
    // OGN's figures on the same silicon (oss/nrf52-ogn-tracker
    // src/ogn-radio.cpp:77-78, 778, 845-851): the average starts at -105 dBm,
    // every live sample moves it by 0.05 of the residual, the channel is clear
    // when the live level is under the average plus 10 dB, and each failed
    // attempt buys 3 dB more tolerance.
    static constexpr int8_t kSeedDbm = -105;
    static constexpr int32_t kWeightPercent = 5;
    static constexpr int8_t kClearMarginDb = 10;
    static constexpr int8_t kRetryStepDb = 3;

    // INFO: fc 03aug26 The ceiling is a derivation, not a number anyone chose.
    // EN 300 220-2 V3.3.1 §4.5.1.3 fixes the polite-access receiver sensitivity
    // limit for a 200 kHz channel; §4.6.2.3 lets the clear-channel threshold sit
    // that far above it below 100 mW e.r.p. and then corrects the result by the
    // antenna gain. Change the declared channel and the first figure moves;
    // change the antenna and the third does. ADS-L 4 SRD-860 issue 2 §C.2 is
    // what makes the channel 200 kHz wide.
    static constexpr int32_t kChannelBandwidthKhz = 200;
    static constexpr int8_t kPoliteSensitivityDbm = -94;
    static constexpr int8_t kPoliteThresholdMarginDb = 15;
    // TODO: fc 03aug26 G8 has not chosen the 868 MHz antenna. This is the most
    // gain we would accept from it, so it is the lowest ceiling the choice can
    // produce and a measured antenna below it only leaves headroom. When G8
    // closes, move this figure and nothing else.
    static constexpr int8_t kAssumedAntennaGainDbd = 3;
    static constexpr int8_t kThresholdCeilingDbm = static_cast<int8_t>(
        kPoliteSensitivityDbm + kPoliteThresholdMarginDb - kAssumedAntennaGainDbd);
    // A ceiling above the zero-gain limit of §4.6.2.3 is a ceiling no antenna
    // choice can justify, so it cannot be raised without editing the derivation.
    static_assert(kThresholdCeilingDbm <= kPoliteSensitivityDbm + kPoliteThresholdMarginDb,
                  "the carrier-sense ceiling exceeds EN 300 220-2 V3.3.1 §4.6.2.3");
    static_assert(kAssumedAntennaGainDbd >= 0, "a negative gain assumption raises the ceiling");

    void sample(int8_t rssi_dbm);

    int8_t dbm() const;
    int8_t threshold_dbm(uint8_t retry = 0) const;
    uint32_t samples() const { return samples_; }

   private:
    int32_t centi_dbm_{static_cast<int32_t>(kSeedDbm) * 100};
    uint32_t samples_{0};
};

// INFO: fc 03aug26 EN 300 220-2 V3.3.1 §4.6.3.2 measures the clear-channel
// assessment over at least 160 us; one instantaneous reading is not an
// assessment. The SX1262 has no receive-signal averaging block, GetRssiInst is
// an instant by definition (DS 13.5.2), and its channel-activity detector
// answers for a LoRa preamble, not for a GFSK carrier (oss/SoftRF-moshe-braner
// .../libraries/RadioLib/src/modules/SX126x/SX126x.h:367). So the interval is a
// run of GetRssiInst reads, and this is what turns them into the one figure the
// clause asks for. The clause asks for average received POWER, which is not
// the average of the readings in dBm: a window is averaged in the linear domain
// and converted back, rounding toward the louder decibel so the assessment can
// only ever defer more often than the exact figure would.
class CarrierSense {
   public:
    static constexpr uint32_t kAssessmentUs = 160;
    static constexpr uint8_t kSamples = 9;
    static constexpr uint32_t kSampleSpacingUs = kAssessmentUs / (kSamples - 1);
    static constexpr uint8_t kMaxSamples = 16;
    static_assert(kSamples >= 2 && kSamples <= kMaxSamples, "a window is at least two readings");
    static_assert(kSampleSpacingUs * (kSamples - 1) >= kAssessmentUs,
                  "the window is shorter than EN 300 220-2 V3.3.1 §4.6.3.2 allows");

    // No readings at all is not a clear channel: 0 dBm refuses everything.
    static int8_t mean_dbm(const int8_t* samples, uint8_t n);
};

// Air time per rolling hour, which is the window EN 300 220-2 measures a duty
// cycle over. The 1% limit is the channel-access route this product declares,
// and §D.3's listen-before-talk is a protocol behaviour on top of it rather
// than an argument for leaving it: this is the evidence for the declaration.
// Sixty one-minute buckets, each one stamped with the minute it holds, so a
// bucket that fell out of the window contributes nothing without anything
// having to sweep it.
class AirTime {
   public:
    static constexpr uint32_t kWindowMs = 3600000;
    static constexpr uint8_t kBuckets = 60;
    static constexpr uint32_t kBucketMs = kWindowMs / kBuckets;
    static constexpr uint32_t kLimitPermille = 10;
    static constexpr uint32_t kBudgetMs = kWindowMs / 1000 * kLimitPermille;

    void spend(uint32_t now_ms, uint32_t air_ms);

    uint32_t window_ms(uint32_t now_ms) const;
    uint32_t permille(uint32_t now_ms) const { return window_ms(now_ms) * 1000u / kWindowMs; }
    bool may_spend(uint32_t now_ms, uint32_t air_ms) const {
        return window_ms(now_ms) + air_ms <= kBudgetMs;
    }

    uint32_t bursts() const { return bursts_; }
    uint32_t total_ms() const { return total_ms_; }

   private:
    static uint32_t minute_of(uint32_t now_ms) { return now_ms / kBucketMs; }

    uint32_t ms_[kBuckets]{};
    // The minute a bucket holds, plus one: zero is a bucket nothing was ever
    // spent in, which is not the same as the minute numbered zero.
    uint32_t stamp_[kBuckets]{};
    uint32_t bursts_{0};
    uint32_t total_ms_{0};
};

}  // namespace skyblip::timing

#endif
