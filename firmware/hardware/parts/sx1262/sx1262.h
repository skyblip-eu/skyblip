// hardware/parts/sx1262/sx1262.h: the SX1262 radio driver, written ONCE and shared
#ifndef SKYBLIP_HARDWARE_PARTS_SX1262_H
#define SKYBLIP_HARDWARE_PARTS_SX1262_H

#include <cstddef>
#include <cstdint>

#include "core/util/result.h"
#include "hardware/io/io.h"

namespace skyblip::parts {

enum class RadioMode : uint8_t { Sleep, Standby, Tx, Rx };

enum class RadioEventType : uint8_t { None, RxDone, CrcError, TxDone, Timeout, Fault };

struct RadioEvent {
    RadioEventType type{RadioEventType::None};
    uint8_t len{0};
    int8_t rssi_dbm{0};
};

// One dwell's worth of modem: where to tune, how the air is modulated there,
// what to match and how much to read behind the match. Two bands share this
// part, and they are two modulations, so nothing here is allowed to be a
// hard-coded M-band figure.
struct RadioConfig {
    uint32_t freq_hz{868200000};
    uint32_t bitrate{100000};
    uint32_t fdev_hz{50000};
    uint32_t bandwidth_hz{200000};
    // The Gaussian filter's BT in hundredths. Zero is an unshaped carrier, which
    // is what the M band asks for and what the part does out of reset.
    uint16_t gaussian_bt_e2{0};
    // The detector runs on chips, so both of these are chip counts: the pattern
    // to match and the fixed number of bytes to read behind it.
    const uint8_t* sync{nullptr};
    uint8_t sync_bits{0};
    uint8_t payload_bytes{0};
    // Tenths of a ppm added to freq_hz before the PLL word is computed, positive
    // upwards. Zero is the design intent on a TCXO part; the field exists so a
    // batch whose reference is out can be corrected in the field instead of
    // returned. Clamped to sx::kFreqTrimLimitTenthsPpm either way.
    int16_t freq_corr_e1_ppm{0};
};

class Sx1262 {
   public:
    Sx1262(io::Spi& spi, io::Gpio& gpio, int busy_pin, int reset_pin, int dio1_pin)
        : spi_(spi), gpio_(gpio), busy_(busy_pin), reset_(reset_pin), dio1_(dio1_pin) {}

    // Is there a radio on the other end of this bus at all? Answers only what a
    // register round-trip can prove, and leaves the chip in standby.
    Status probe();

    Status begin();
    Status configure_radio(const RadioConfig& cfg);

    Status transmit(const uint8_t* data, uint8_t len);
    Status start_receive();

    // The lowest-power state the part has, and the way back out of it.
    void sleep();
    Status wake();

    // Instantaneous RSSI on the tuned channel, for listen-before-talk. Valid
    // only while the receiver is running (DS 13.5.2).
    int8_t rssi_inst();

    RadioEvent poll(uint8_t* rx_buf, uint8_t cap);

    bool service(uint32_t elapsed_ms, uint32_t no_rx_reinit_ms = 30000);

    RadioMode mode() const { return mode_; }
    uint32_t reinit_count() const { return reinit_count_; }
    // A transmission the chip's own SetTx timeout had to end. Never silent.
    uint32_t tx_recovery_count() const { return tx_recovery_count_; }

   private:
    Status wait_busy_low(uint32_t max_spins = 100000);
    void cmd(uint8_t opcode, const uint8_t* params, size_t n);
    void cmd_read(uint8_t opcode, uint8_t* out, size_t n);
    void write_register(uint16_t addr, const uint8_t* data, size_t n);
    void read_register(uint16_t addr, uint8_t* out, size_t n);
    void hold_reset_low();
    Status reset_to_standby();
    Status verify_link();
    Status enter_standby();
    void configure_modulation(const RadioConfig& cfg);
    void configure_rx_gain();
    void configure_power();
    void configure_irq();
    void configure_frame(const RadioConfig& cfg);
    uint32_t tx_timeout_ticks(uint8_t len) const;
    void recover_tx();
    Status reinit();

    io::Spi& spi_;
    io::Gpio& gpio_;
    int busy_, reset_, dio1_;
    RadioMode mode_{RadioMode::Sleep};
    RadioConfig cfg_{};
    bool configured_{false};
    uint32_t ms_since_rx_{0};
    uint32_t reinit_count_{0};
    uint32_t tx_recovery_count_{0};
};

namespace sx {
constexpr uint8_t kSetStandby = 0x80;
constexpr uint8_t kSetRegulatorMode = 0x96;
// DS 13.1.4 regModeParam. The part comes out of reset on its LDO alone and the
// converter roughly halves the supply current in receive and in transmit, which
// on a dwell map that arms the receiver through about 980 ms of every second is
// the largest single power term on the board. DC-DC needs the inductor on
// DCC_SW to be fitted: SoftRF issues SetRegulatorMode(REGMODE_DCDC) as the first
// line of its SX126x bring-up on this hardware
// (libraries/arduino-basicmac/src/lmic/radio-sx126x.c:703, 855), which is the
// evidence we have that the module carries it. Bench current in receive is what
// closes it.
constexpr uint8_t kRegulatorLdo = 0x00;
constexpr uint8_t kRegulatorDcDc = 0x01;
constexpr uint8_t kSetDio3AsTcxoCtrl = 0x97;
constexpr uint8_t kSetDio2AsRfSwitch = 0x9D;
constexpr uint8_t kCalibrate = 0x89;
constexpr uint8_t kTcxoVolt1v8 = 0x02;  // DIO3 TCXO 1.8 V (T-Echo Plus)
constexpr uint8_t kCalibrateImage = 0x98;
constexpr uint8_t kSetTx = 0x83;
constexpr uint8_t kSetRx = 0x82;
constexpr uint8_t kSetRfFrequency = 0x86;
constexpr uint8_t kSetPacketType = 0x8A;
constexpr uint8_t kSetModulationParams = 0x8B;
constexpr uint8_t kSetPacketParams = 0x8C;
constexpr uint8_t kSetPaConfig = 0x95;
constexpr uint8_t kSetTxParams = 0x8E;
constexpr uint8_t kSetDioIrqParams = 0x08;
constexpr uint8_t kWriteRegister = 0x0D;
constexpr uint8_t kReadRegister = 0x1D;
constexpr uint8_t kSetSleep = 0x84;
// DS 13.1.2 sleepConfig: bit 0 is the RTC wake-up, bit 2 is warm start. Warm
// start keeps the configuration in retention, so coming back is an NSS edge and
// a standby rather than the whole bring-up; the RTC is off because the only
// thing that wakes this device is the button.
constexpr uint8_t kSleepWarmStartNoRtc = 0x04;
constexpr uint16_t kSyncWordRegister = 0x06C0;  // DS 13.4.9, 8 bytes

// DS 9.6 ("Rx Gain") register 0x08AC: 0x94 is the power-saving gain the part
// comes out of reset on, 0x96 the boosted one. The figures for what the boost
// buys are the references', not the datasheet's - about 2 mA more supply current
// in receive for about 3 dB of sensitivity, in SoftRF's own words on the line
// that writes it (arduino-basicmac/src/lmic/radio-sx126x.c:365-372) - so both
// halves of that trade are a bench measurement we owe.
//
// We take it, and it is a decision rather than an inherited default. 3 dB is
// about 40% more range on a device whose whole job is to hear an aircraft before
// it matters, and 2 mA is under a tenth of what the DC-DC converter above just
// gave back on the same 980 ms of every second. SoftRF writes the boosted value
// on every SetRx, and openace turns it on in both of its receive paths
// (src/lib/sx1262/ace/src/sx1262.cpp:216, 380), so the value itself is what this
// hardware is known to run on.
//
// The one caveat we could find is not that SetRx resets the register: it is that
// 0x08AC is not part of what a warm start retains unless it is added to the
// retention list at 0x029F (DS 9.6, just below table 9-3, which is the citation
// RadioLib's SX126x_config.cpp:405-409 carries for the same three bytes). Our
// sleep IS a warm start, so a radio that slept and came back would be a radio on
// the power-saving gain again, with no symptom beyond a shorter range. Both
// halves are therefore written: the retention list once in begin(), and the
// register itself in every configure_radio(), which is the standby bracket every
// dwell passes through anyway (register access outside standby is what the model
// refuses, DS 13.1).
constexpr uint16_t kRxGainRegister = 0x08AC;
constexpr uint8_t kRxGainPowerSaving = 0x94;
constexpr uint8_t kRxGainBoosted = 0x96;
// DS 9.6: the retention list is three bytes at 0x029F - a count and one 16-bit
// register address.
constexpr uint16_t kRetentionListRegister = 0x029F;
constexpr uint8_t kRetainRxGain[3] = {0x01, static_cast<uint8_t>(kRxGainRegister >> 8),
                                      static_cast<uint8_t>(kRxGainRegister & 0xFF)};
// Written and read back over the same register to prove the part answers. Two
// bytes that are each other's complement: a MISO line stuck at either rail, or
// tied to MOSI, gives back something else.
constexpr uint8_t kLinkProbePattern[2] = {0xA5, 0x5A};
constexpr uint8_t kWriteBuffer = 0x0E;
constexpr uint8_t kReadBuffer = 0x1E;
constexpr uint8_t kGetIrqStatus = 0x12;
constexpr uint8_t kClearIrqStatus = 0x02;
constexpr uint8_t kGetStatus = 0xC0;
constexpr uint8_t kGetRssiInst = 0x15;
constexpr uint8_t kGetRxBufferStatus = 0x13;
constexpr uint8_t kGetPacketStatus = 0x14;
constexpr uint16_t kIrqTxDone = 0x0001;
constexpr uint16_t kIrqRxDone = 0x0002;
constexpr uint16_t kIrqCrcErr = 0x0040;
constexpr uint16_t kIrqTimeout = 0x0200;
// INFO: wr 02aug26 DS 13.3.1: IrqMask is 0x0000 out of reset and gates the whole
// IRQ status register, so an unmasked bit is a bit GetIrqStatus never reports.
constexpr uint16_t kIrqMask = kIrqTxDone | kIrqRxDone | kIrqCrcErr | kIrqTimeout;

constexpr uint32_t kXtalHz = 32000000;
// DS 13.4.6 GFSK modulation params: pulse shape and RX bandwidth are table
// indices. 0x0A is the 234.3 kHz double-sideband entry.
constexpr uint8_t kPulseShapeNone = 0x00;
constexpr uint8_t kGaussianBt0p3 = 0x08;
constexpr uint8_t kGaussianBt0p5 = 0x09;
constexpr uint8_t kGaussianBt0p7 = 0x0A;
constexpr uint8_t kGaussianBt1p0 = 0x0B;
constexpr uint8_t kRxBandwidth234kHz = 0x0A;

// DS 13.4.6, the double-sideband RX bandwidth table. The driver picks the
// narrowest entry that still passes the channel it was asked for: too narrow
// clips the signal, too wide buys noise. Transcribed from the datasheet in the
// order the part numbers them, and only over the range these two bands use.
struct RxBandwidthEntry {
    uint32_t hz;
    uint8_t index;
};
constexpr RxBandwidthEntry kRxBandwidths[] = {
    {117300, 0x0B}, {156200, 0x1A}, {187200, 0x12}, {234300, 0x0A},
    {312000, 0x19}, {373600, 0x11}, {467000, 0x09},
};
constexpr int kRxBandwidthCount =
    static_cast<int>(sizeof(kRxBandwidths) / sizeof(kRxBandwidths[0]));

// DS 13.1.14 SetPaConfig for the SX1262 high-power PA. This is also the write
// that raises the over-current protection to 140 mA.
constexpr uint8_t kPaConfigHighPower[4] = {0x04, 0x07, 0x00, 0x01};
constexpr uint8_t kRampTime200Us = 0x04;
// INFO: wr 02aug26 ERC 70-03 annex 1 band h1.4 / EN 300 220: 868.0-868.6 MHz is
// 25 mW e.r.p., which is 14 dBm. The ceiling, not a chip default.
constexpr int8_t kSrd868ErpLimitDbm = 14;

// What the chip is told is CONDUCTED power at its own output, and the limit
// above is radiated, referenced to a half-wave dipole. The two differ by the
// feed and by the antenna, so the register value is only defensible with the
// arithmetic written down. Hundredths of a dB, because the dBi-to-dBd step is
// 2.15 dB and rounding it away is how a compliance argument goes quietly wrong.
//
// TODO: fc 03aug26 Both antenna figures are the paper part of gate G8
// (project/research/antenna-868-go.md: ANT-868-CW-QW-SMA, 1.6 dBi peak, and an
// unmeasured 0.5 dB allowance for the U.FL-to-SMA feed). Replace them with the
// VNA measurement before the regulatory file is filed; the assertion below is
// what tells you the moment the answer stops holding.
constexpr int16_t kDbiToDbdCentiDb = 215;
constexpr int16_t kAntennaPeakGainDbiCentiDb = 160;
constexpr int16_t kFeedLossCentiDb = 50;
// What SetTxParams is given. A quarter-wave whip sits below a dipole, so the
// part runs out of power before the regulation does.
constexpr int8_t kConductedDbm = 14;
constexpr int16_t kResultingErpCentiDb = static_cast<int16_t>(
    kConductedDbm * 100 - kFeedLossCentiDb + kAntennaPeakGainDbiCentiDb - kDbiToDbdCentiDb);
static_assert(kResultingErpCentiDb <= kSrd868ErpLimitDbm * 100,
              "the programmed conducted power exceeds 25 mW e.r.p. with the declared antenna");

// DS 13.1.12 CalibrateImage, the 863-870 MHz band pair.
constexpr uint8_t kImageBand863to870[2] = {0xD7, 0xDB};

// The most the programmed centre frequency may be trimmed, in tenths of a ppm.
// At 868.2 MHz a tenth of a ppm is 87 Hz, so this is +-8.7 kHz: enough to
// correct a reference ten times worse than the TCXO this board fits, and small
// against both the 200 kHz channel and the distance from 868.2 MHz to the edges
// of ERC 70-03 band h1.4 (868.0-868.6 MHz), so no legal trim moves the carrier
// out of the band it is licensed in. Kept equal to
// settings::kFreqTrimLimitTenthsPpm, which is the same bound on the stored
// value; test/hardware/test_sx1262_level.cpp pins the two together.
constexpr int16_t kFreqTrimLimitTenthsPpm = 100;
// Tenths of a ppm are parts in 10^7.
constexpr int32_t kFreqTrimPerUnit = 10000000;

// The frequency actually programmed, once the reference's own error is taken
// out. Positive trim moves the carrier up, which is what a part measuring low
// on a counter needs.
constexpr uint32_t trimmed_hz(uint32_t freq_hz, int16_t corr_e1_ppm) {
    int32_t corr = corr_e1_ppm;
    if (corr > kFreqTrimLimitTenthsPpm) corr = kFreqTrimLimitTenthsPpm;
    if (corr < -kFreqTrimLimitTenthsPpm) corr = -kFreqTrimLimitTenthsPpm;
    const int64_t offset = (static_cast<int64_t>(freq_hz) * corr) / kFreqTrimPerUnit;
    return static_cast<uint32_t>(static_cast<int64_t>(freq_hz) + offset);
}

// DS 13.4.1: the SetTx timeout counts 15.625 us steps, 24 bits wide. 0 disables
// it, which is a PA that stays keyed when TxDone never arrives.
constexpr uint32_t kTimeoutStepNs = 15625;
constexpr uint32_t kTimeoutTicksMax = 0xFFFFFF;
constexpr uint32_t kTxGuardUs = 25000;

// INFO: wr 02aug26 DS 8.1: NRESET must be held low >= 100 us. io::Gpio has no
// delay primitive, so the pulse is a bounded spin on BUSY sized against the
// slowest credible cost of one such read on nRF52840 at 64 MHz.
constexpr uint32_t kResetLowUs = 100;
constexpr uint32_t kResetSpinNsFloor = 125;
constexpr uint32_t kResetLowSpins = kResetLowUs * 1000u / kResetSpinNsFloor;
// §C.2 puts 16 chips of preamble before the sync word. Eight of them are enough
// for the detector to declare a preamble.
constexpr uint16_t kPreambleChips = 16;
constexpr uint8_t kPreambleDetect8Chips = 0x04;
constexpr uint8_t kAddrCompOff = 0x00;
constexpr uint8_t kFixedLength = 0x00;
constexpr uint8_t kCrcOff = 0x00;
constexpr uint8_t kWhiteningOff = 0x00;
}

}

#endif
