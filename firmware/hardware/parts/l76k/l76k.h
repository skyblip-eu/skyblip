// A GNSS receiver has no registers to drive: it produces. This driver owns the
// UART and the NMEA parser and hands out a fix, which the board pushes onto the bus.
// It also owns the two things the receiver will not do for us: come up in the
// aviation dynamic model, on the constellations and at the rate we need, and say
// whether what it is producing is a fix at all.
#ifndef SKYBLIP_HARDWARE_PARTS_L76K_H
#define SKYBLIP_HARDWARE_PARTS_L76K_H

#include "core/gnss/nmea.h"
#include "core/gnss/validity.h"
#include "hardware/io/io.h"

namespace skyblip::parts {

// The rate port is io::UartRate (hardware/io/io.h): a board whose platform can
// retune the port hands the driver one, and autobaud recovery becomes available.

class L76k {
   public:
    explicit L76k(io::Uart& uart, io::UartRate& rate = io::kFixedUartRate) : uart_(uart), rate_(rate) {}

    enum class Config : uint8_t {
        Idle,
        Restarting,
        Waking,
        Identifying,
        Sending,
        Verifying,
        Ready,
        Degraded
    };

    // What $PCAS10 asks the receiver to throw away. A poisoned almanac otherwise
    // costs a pilot twenty minutes of a receiver that reads as broken, and there
    // is no other way out of it from the front panel. SoftRF carries the same
    // escape for u-blox only (.../src/driver/GNSS.cpp, ENABLE_UBLOX_RFS).
    enum class Restart : uint8_t { Hot = 0, Warm = 1, Cold = 2, Factory = 3 };

    // INFO: gn 09Jun25 The devicetree pins this UART, and the receiver's factory
    // rate is 1 Hz. t_echo_plus.dts:278 `current-speed = <9600>` must equal
    // kBaudRate: nothing in the build checks it, so the two are named here to be
    // greppable. One GGA + one RMC is ~150 bytes, 156 ms of line time at 9600
    // baud, which is what caps kFixRateHz at 5 Hz on this wiring.
    static constexpr uint32_t kBaudRate = 9600;

    // INFO: fc 03aug26 A receiver that comes up at another rate (a returned unit
    // reflashed by someone else, a module whose backup domain kept a $PCAS01) is
    // silently GNSS-less forever if 9600 is an assumption. moshe-braner walks the
    // rates until NMEA appears (.../src/driver/GNSS.cpp:1700-1739); OGN cycles to
    // the next rate after 2 s with no valid data (src/gps.cpp:89-96, 1205-1222).
    // Ours is the AT6558's own list, most likely first.
    static constexpr int kBaudCandidateCount = 6;
    static constexpr uint32_t kBaudCandidates[kBaudCandidateCount] = {
        9600,
        115200,
        38400,
        57600,
        19200,
        4800,
    };

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

    // INFO: fc 03aug26 The L76K wakes on UART activity, so a receiver that is
    // asleep when we start talking eats the first thing we say. SoftRF sends one
    // 0x00 and waits 500 ms before it probes anything (oss/SoftRF-lyusupov
    // .../src/driver/GNSS.cpp:1383-1387).
    static constexpr uint8_t kWakeByte = 0x00;
    static constexpr uint32_t kWakeDelayMs = 500;

    // INFO: fc 03aug26 $PCAS06 asks the receiver to name itself and it answers
    // "$GPTXT,01,01,02,SW=<version>". SoftRF treats that exchange as the proof
    // the part is an AT6558 before it trusts a single $PCAS sentence
    // (.../src/driver/GNSS.cpp:981-1010), and logs the version for support.
    static constexpr const char* kIdentifyCommand = "$PCAS06,0*1B\r\n";
    static constexpr uint32_t kIdentifyWindowMs = 1000;

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

    // $PCAS03 field 3 is GSA and it stays off. No GSA is no VDOP, which is why
    // core/protocol/adsl.cpp set_integrity_from_hdop_e2 substitutes HDOP for it:
    // the vertical figure is the larger per unit of DOP, so the substitution
    // costs accuracy claim rather than inventing one. Turning GSA on would cost a
    // third of the line budget kFixRateHz already spends.
    static constexpr bool kGsaEnabled = false;

    // $PCAS10 reboots the receiver. It answers nothing for about a second after
    // it, so the sequence behind a factory reset waits before it starts talking.
    static constexpr uint32_t kRestartSettleMs = 1000;
    static constexpr const char* kRestartCommands[4] = {
        "$PCAS10,0*1C\r\n",
        "$PCAS10,1*1D\r\n",
        "$PCAS10,2*1E\r\n",
        "$PCAS10,3*1F\r\n",
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
    // that produced a NEW fix, or when the fix we already published stopped
    // being one: a receiver that goes silent has to be reported, and it says
    // nothing by definition.
    bool poll(uint32_t now_ms);

    // The board polls immediately after service(), so the instant of the last
    // service call is this poll's instant. Ages measured from anywhere else
    // would be measured from a clock this part does not have.
    bool poll() { return poll(serviced_ms_); }

    const gnss::GnssFix& fix() const { return fix_; }

    // Why the last solution was not a fix, and how many times that has happened.
    // Both are for the self-test page and a support case, and nothing else reads
    // them: the fix's own `valid` is the answer every service uses.
    gnss::FixReject reject_reason() const { return validity_.last_reject(); }
    uint32_t rejected() const { return validity_.rejected(); }

    // The rate we are actually talking to the receiver at, which is only the
    // devicetree's rate until autobaud has had to move.
    uint32_t baud_rate() const { return kBaudCandidates[baud_index_]; }

    // Did the part name itself, and as what. An unidentified receiver still gets
    // the $PCAS sequence, because the alternative is no configuration at all,
    // but the self-test says so and a support case has the firmware string.
    bool identified() const { return parser_.identified(); }
    const char* firmware_version() const { return parser_.firmware_version(); }

    // Throw away the receiver's stored state. A factory reset takes our
    // configuration with it, so the sequence runs again behind it.
    void request_restart(Restart kind);

    // Sentences the parser accepted since boot. A receiver that is wired but
    // silent (or babbling at the wrong baud) never moves this off zero, which is
    // what the DFU health gate watches.
    uint32_t updates() const { return parser_.fix().updates; }

   private:
    static constexpr size_t kChunk = 64;

    static constexpr uint8_t kNoRestart = 0xFF;

    void start_sequence(uint32_t now_ms);
    void send_next(uint32_t now_ms);
    void begin_wake(uint32_t now_ms);
    void send(const char* sentence, uint32_t now_ms);
    void verify_failed(uint32_t now_ms);
    bool next_baud();

    io::Uart& uart_;
    io::UartRate& rate_;
    gnss::NmeaParser parser_{};
    gnss::FixValidity validity_{};
    gnss::GnssFix fix_{};
    uint32_t applied_{0};

    Config state_{Config::Idle};
    uint32_t serviced_ms_{0};
    uint32_t last_command_ms_{0};
    uint32_t verify_start_ms_{0};
    uint32_t verify_updates_{0};
    int next_command_{0};
    int baud_index_{0};
    uint8_t baud_tried_{1};
    uint8_t attempts_{0};
    uint8_t pending_restart_{kNoRestart};
};

}  // namespace skyblip::parts

#endif
