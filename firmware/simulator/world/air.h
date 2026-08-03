#ifndef SKYBLIP_SIMULATOR_WORLD_AIR_H
#define SKYBLIP_SIMULATOR_WORLD_AIR_H

#include <cstdint>

#include "core/protocol/air.h"
#include "core/timing/slot.h"
#include "hardware/parts/sx1262/model.h"

namespace skyblip::simulator {

enum class AirEvent : uint8_t {
    // Own-ship put a burst on air.
    Tx,
    // A burst arrived while the radio was tuned to its channel and listening.
    Rx,
    // A burst arrived while the radio was elsewhere: wrong band, wrong channel,
    // transmitting, between dwells, or listening for a sync word this burst does
    // not carry. This is what a slot-timing bug looks like.
    Deaf,
    // Two bursts overlapped on the same channel.
    Collision,
};

// The longest burst the channel carries: an O-band uplink frame, which is not
// Manchester-coded and is eleven times what an M-band one is.
constexpr int kMaxBurstBytes = protocol::kUplinkBurstBytes;
static_assert(protocol::kTxChipBytes <= kMaxBurstBytes, "an M-band burst would not fit the air");

struct AirRecord {
    uint64_t at_us{0};
    uint32_t freq_hz{0};
    // The chip rate the burst was sent at. It is part of the burst, not of the
    // receiver: the two bands are two modulations and a dwell framing the wrong
    // one is deaf on air, which is a bug the tape has to be able to show.
    uint32_t bitrate{protocol::kMbandChipRateBps};
    uint16_t phase_ms{0};
    uint16_t len{0};
    AirEvent event{AirEvent::Deaf};
    int8_t rssi_dbm{0};
    uint8_t chips[kMaxBurstBytes]{};
};

// The 868 MHz channel between the modelled radio and everything else flying.
// A frame exists at an instant, on a frequency, for as long as it takes to send
// it. Whether the radio hears it is decided by where the radio was tuned when
// the burst started, never by who queued it. Every burst is logged, heard or
// not, which is what makes slot timing falsifiable instead of assumed.
class Air {
   public:
    // §C.2 at 100 kchip/s: 16-chip preamble, 64-chip Manchester sync word and
    // 25 Manchester-encoded bytes.
    static constexpr uint64_t kAirTimeUs = 4800;
    static constexpr int8_t kNoiseFloorDbm = -110;
    // §C.2: 200 kHz between M-band channels, so anything within half of that is
    // in the receiver's passband and the exact PLL word does not matter.
    static constexpr uint32_t kChannelToleranceHz = 100000;
    static constexpr int8_t kSensitivityDbm = -115;
    static constexpr int kMaxBursts = 16;
    static constexpr int kLogSize = 96;

    static bool tuned_to(uint32_t radio_hz, uint32_t burst_hz) {
        return (radio_hz > burst_hz ? radio_hz - burst_hz : burst_hz - radio_hz) <
               kChannelToleranceHz;
    }

    // How long a burst of this many bytes occupies the channel at this chip
    // rate. Derived rather than remembered, because the O band sends eleven
    // times the bytes at twice the rate and a fixed figure would be wrong for
    // one of the two.
    static uint64_t air_time_us(uint16_t len, uint32_t bitrate) {
        return static_cast<uint64_t>(len) * 8u * 1000000u / bitrate;
    }

    // A burst is chips on a frequency, at a chip rate: the sync word the
    // transmitter used is part of them, which is what decides whether a
    // listening receiver frames it, and so is the rate it clocked them out at.
    void emit(uint64_t at_us, uint32_t freq_hz, const uint8_t* chips, uint16_t len, int8_t rssi_dbm,
              uint32_t bitrate = protocol::kMbandChipRateBps);

    // Drive the channel up to now_us: own-ship transmissions are picked up from
    // the radio, bursts that started are judged heard or not, bursts that ended
    // are handed to the receiver, and the carrier the radio would measure is
    // set for the listen-before-talk sample that follows.
    void step(uint64_t now_us, models::Sx1262& radio);

    // What a receiver armed with the shared sync window would frame out of a
    // logged M-band burst: the tape is chips, and reading it means detecting the
    // sync exactly as the radio does.
    static bool framed(const AirRecord& record, protocol::Frame& out);

    // The same for the O band, where there is no Manchester and no shared
    // window: strip §C.4.3's sync word and the length byte behind it, and what
    // is left is the codeword.
    static bool framed_uplink(const AirRecord& record, uint8_t* frame);

    void clear();

    int record_count() const { return count_ < kLogSize ? count_ : kLogSize; }
    const AirRecord& record(int i) const;
    int format(int i, char* out, int cap) const;
    uint32_t heard() const { return heard_; }
    uint32_t deaf() const { return deaf_; }
    uint32_t collisions() const { return collisions_; }

   private:
    struct Burst {
        bool used{false};
        bool started{false};
        bool heard{false};
        bool collided{false};
        bool mine{false};
        uint64_t at_us{0};
        uint64_t air_time_us{0};
        uint32_t freq_hz{0};
        uint32_t bitrate{protocol::kMbandChipRateBps};
        int8_t rssi_dbm{0};
        uint16_t len{0};
        uint8_t chips[kMaxBurstBytes]{};
    };

    void log(const Burst& b, AirEvent event);
    void take_own_transmission(uint64_t now_us, models::Sx1262& radio);
    void set_carrier(uint64_t now_us, models::Sx1262& radio);

    Burst burst_[kMaxBursts]{};
    AirRecord log_[kLogSize]{};
    int count_{0};
    uint32_t heard_{0};
    uint32_t deaf_{0};
    uint32_t collisions_{0};
    uint64_t tx_done_at_us_{0};
    bool tx_in_flight_{false};
};

}  // namespace skyblip::simulator

#endif
