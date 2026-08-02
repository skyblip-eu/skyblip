// A GNSS receiver has no registers to drive: it produces. This driver owns the
// UART and the NMEA parser and hands out a fix, which the board pushes onto the bus.
// It also owns the one thing the receiver will not do for us: come up in the
// aviation dynamic model, on the constellations and at the rate we need.
#ifndef SKYBLIP_HARDWARE_PARTS_L76K_H
#define SKYBLIP_HARDWARE_PARTS_L76K_H

#include "core/gnss/nmea.h"
#include "hardware/io/io.h"

namespace skyblip::parts {

class L76k {
   public:
    explicit L76k(io::Uart& uart) : uart_(uart) {}

    enum class Config : uint8_t { Idle, Sending, Verifying, Ready, Degraded };

    // INFO: gn 09Jun25 The devicetree pins this UART, and the receiver's factory
    // rate is 1 Hz. t_echo_plus.dts:278 `current-speed = <9600>` must equal
    // kBaudRate: nothing in the build checks it, so the two are named here to be
    // greppable. One GGA + one RMC is ~150 bytes, 156 ms of line time at 9600
    // baud, which is what caps kFixRateHz at 5 Hz on this wiring.
    static constexpr uint32_t kBaudRate = 9600;

    // ADS-L 4 SRD860 issue 2 G.1.16 refuses a navigation solution older than
    // 500 ms (timing::Transmitter::kFixAgeMaxMs), and the direct slot runs to
    // 1000 ms after the second. At the factory 1 Hz roughly half of all
    // transmissions are suppressed, which on the bench reads as an intermittent
    // transmitter rather than a configuration bug.
    static constexpr uint32_t kFixRateHz = 5;
    static constexpr uint32_t kFixPeriodMs = 1000 / kFixRateHz;

    // INFO: gn 09Jun25 The NMEA burst is late relative to the PPS edge whose
    // second it describes. SoftRF carries this per chip and subtracts it from the
    // sentence's arrival time (oss/SoftRF-lyusupov .../src/driver/GNSS.cpp:1072-1078
    // at65_ops = {70 GGA, 135 RMC}, applied at .../src/driver/RF.cpp:236-260); OGN
    // has the same thing as PPSdelay, default 100 ms. The burst is only complete
    // once both sentences are in, so the later of the two governs.
    static constexpr uint16_t kPpsLatencyMs = 135;

    // INFO: gn 09Jun25 SoftRF sends exactly these three to this part, 250 ms
    // apart (oss/SoftRF-lyusupov .../src/driver/GNSS.cpp:1029-1057): GPS +
    // GLONASS + BeiDou, GGA + RMC only, and the aviation dynamic model. Without
    // the last one the receiver applies pedestrian smoothing and lags in turns.
    // $PCAS02 is ours: SoftRF leaves the rate at the factory 1 Hz.
    static constexpr uint32_t kCommandGapMs = 250;
    static constexpr int kCommandCount = 4;
    static constexpr const char* kCommands[kCommandCount] = {
        "$PCAS04,7*1E\r\n",
        "$PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0*02\r\n",
        "$PCAS11,6*1B\r\n",
        "$PCAS02,200*1D\r\n",
    };

    // Nothing acknowledges a $PCAS sentence, so the acknowledgement we accept is
    // the receiver's cadence: one accepted sentence per solution we asked for,
    // over a window long enough that a 1 Hz receiver cannot fake it.
    static constexpr uint32_t kVerifyWindowMs = 3000;
    static constexpr uint32_t kMinVerifyUpdates = kVerifyWindowMs / kFixPeriodMs;
    static constexpr uint8_t kMaxConfigAttempts = 3;

    // Drive the configuration sequence. Until it has run the receiver is on its
    // factory defaults, which is a degraded receiver wearing a working one's face.
    void service(uint32_t now_ms);

    Config config_state() const { return state_; }
    bool configured() const { return state_ == Config::Ready; }
    bool degraded() const { return state_ == Config::Degraded; }

    // Drain whatever the receiver has said into the parser. Returns true when
    // that produced a NEW fix, so the caller never re-applies a stale one.
    bool poll();

    const gnss::GnssFix& fix() const { return fix_; }

    // Sentences the parser accepted since boot. A receiver that is wired but
    // silent (or babbling at the wrong baud) never moves this off zero, which is
    // what the DFU health gate watches.
    uint32_t updates() const { return parser_.fix().updates; }

   private:
    static constexpr size_t kChunk = 64;

    void start_sequence(uint32_t now_ms);
    void send_next(uint32_t now_ms);

    io::Uart& uart_;
    gnss::NmeaParser parser_{};
    gnss::GnssFix fix_{};
    uint32_t applied_{0};

    Config state_{Config::Idle};
    uint32_t last_command_ms_{0};
    uint32_t verify_start_ms_{0};
    uint32_t verify_updates_{0};
    int next_command_{0};
    uint8_t attempts_{0};
};

}  // namespace skyblip::parts

#endif
