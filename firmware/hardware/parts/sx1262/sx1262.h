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

struct MbandConfig {
    uint32_t freq_hz{868200000};
    uint32_t bitrate{100000};
    uint32_t fdev_hz{50000};
    // The detector runs on chips, so both of these are chip counts: the pattern
    // to match and the fixed number of bytes to read behind it.
    const uint8_t* sync{nullptr};
    uint8_t sync_bits{0};
    uint8_t payload_bytes{0};
};

class Sx1262 {
   public:
    Sx1262(io::Spi& spi, io::Gpio& gpio, int busy_pin, int reset_pin, int dio1_pin)
        : spi_(spi), gpio_(gpio), busy_(busy_pin), reset_(reset_pin), dio1_(dio1_pin) {}

    // Is there a radio on the other end of this bus at all? Answers only what a
    // register round-trip can prove, and leaves the chip in standby.
    Status probe();

    Status begin();
    Status configure_mband(const MbandConfig& cfg);

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
    void configure_modulation(const MbandConfig& cfg);
    void configure_power();
    void configure_irq();
    void configure_frame(const MbandConfig& cfg);
    uint32_t tx_timeout_ticks(uint8_t len) const;
    void recover_tx();
    Status reinit();

    io::Spi& spi_;
    io::Gpio& gpio_;
    int busy_, reset_, dio1_;
    RadioMode mode_{RadioMode::Sleep};
    MbandConfig cfg_{};
    bool configured_{false};
    uint32_t ms_since_rx_{0};
    uint32_t reinit_count_{0};
    uint32_t tx_recovery_count_{0};
};

namespace sx {
constexpr uint8_t kSetStandby = 0x80;
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
constexpr uint8_t kRxBandwidth234kHz = 0x0A;

// DS 13.1.14 SetPaConfig for the SX1262 high-power PA. This is also the write
// that raises the over-current protection to 140 mA.
constexpr uint8_t kPaConfigHighPower[4] = {0x04, 0x07, 0x00, 0x01};
constexpr uint8_t kRampTime200Us = 0x04;
// INFO: wr 02aug26 ERC 70-03 annex 1 band h1.4 / EN 300 220: 868.0-868.6 MHz is
// 25 mW e.r.p., which is 14 dBm. The ceiling, not a chip default.
constexpr int8_t kSrd868ErpLimitDbm = 14;

// DS 13.1.12 CalibrateImage, the 863-870 MHz band pair.
constexpr uint8_t kImageBand863to870[2] = {0xD7, 0xDB};

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
