// devices/drivers/sx1262.h — the SX1262 radio driver, written ONCE and shared
#ifndef SKYBLIP_DEVICES_DRIVERS_SX1262_H
#define SKYBLIP_DEVICES_DRIVERS_SX1262_H

#include <cstddef>
#include <cstdint>

#include "core/util/result.h"
#include "devices/io/io.h"

namespace skyblip::drivers {

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
};

class Sx1262 {
   public:
    Sx1262(io::Spi& spi, io::Gpio& gpio, int busy_pin, int reset_pin, int dio1_pin)
        : spi_(spi), gpio_(gpio), busy_(busy_pin), reset_(reset_pin), dio1_(dio1_pin) {}

    Status begin();
    Status configure_mband(const MbandConfig& cfg);

    Status transmit(const uint8_t* data, uint8_t len);
    Status start_receive();

    RadioEvent poll(uint8_t* rx_buf, uint8_t cap);

    bool service(uint32_t elapsed_ms, uint32_t no_rx_reinit_ms = 30000);

    RadioMode mode() const { return mode_; }
    uint32_t reinit_count() const { return reinit_count_; }

   private:
    Status wait_busy_low(uint32_t max_spins = 100000);
    void cmd(uint8_t opcode, const uint8_t* params, size_t n);
    void cmd_read(uint8_t opcode, uint8_t* out, size_t n);
    Status reinit();

    io::Spi& spi_;
    io::Gpio& gpio_;
    int busy_, reset_, dio1_;
    RadioMode mode_{RadioMode::Sleep};
    MbandConfig cfg_{};
    bool configured_{false};
    uint32_t ms_since_rx_{0};
    uint32_t reinit_count_{0};
};

namespace sx {
constexpr uint8_t kSetStandby = 0x80;
constexpr uint8_t kSetDio3AsTcxoCtrl = 0x97;
constexpr uint8_t kSetDio2AsRfSwitch = 0x9D;
constexpr uint8_t kCalibrate = 0x89;
constexpr uint8_t kTcxoVolt1v8 = 0x02;  // DIO3 TCXO 1.8 V (T-Echo Plus)
constexpr uint8_t kSetTx = 0x83;
constexpr uint8_t kSetRx = 0x82;
constexpr uint8_t kSetRfFrequency = 0x86;
constexpr uint8_t kSetPacketType = 0x8A;
constexpr uint8_t kWriteBuffer = 0x0E;
constexpr uint8_t kReadBuffer = 0x1E;
constexpr uint8_t kGetIrqStatus = 0x12;
constexpr uint8_t kClearIrqStatus = 0x02;
constexpr uint8_t kGetStatus = 0xC0;
constexpr uint8_t kGetRxBufferStatus = 0x13;
constexpr uint16_t kIrqTxDone = 0x0001;
constexpr uint16_t kIrqRxDone = 0x0002;
constexpr uint16_t kIrqCrcErr = 0x0040;
constexpr uint16_t kIrqTimeout = 0x0200;
}

}

#endif
