// hardware/parts/sx1262/model.h — a behavioural SX1262 model at the SPI/GPIO seam, so
// the REAL parts::Sx1262 runs against it unchanged: it answers opcodes, raises
// DIO1 on Rx/Tx-done, and can be stuck busy or made to report a CRC error.
#ifndef SKYBLIP_HARDWARE_MODEL_SX1262_H
#define SKYBLIP_HARDWARE_MODEL_SX1262_H

#include <cstring>
#include <vector>

#include "hardware/io/io.h"
#include "hardware/parts/sx1262/sx1262.h"

namespace skyblip::models {

class Sx1262 : public io::Spi, public io::Gpio {
   public:
    void set(int pin, bool level) override {
        if (pin == reset_pin && level) reset_pulses++;
    }
    bool get(int pin) override {
        if (pin == busy_pin) return busy_stuck;
        if (pin == dio1_pin) return irq_flags != 0;
        return false;
    }
    void mode_output(int) override {}
    void mode_input(int, bool) override {}

    void select(bool on) override {
        if (on) {
            seq_ = 0;
            opcode_ = 0xFF;
        }
    }
    void transfer(const uint8_t* tx, uint8_t* rx, size_t len) override {
        for (size_t i = 0; i < len; i++) {
            uint8_t in = tx ? tx[i] : 0;
            uint8_t out = process(in);
            if (rx) rx[i] = out;
            seq_++;
        }
    }

    void queue_rx(const uint8_t* data, uint8_t len, bool crc_error = false, int8_t rssi = -80) {
        rx_buf_.assign(data, data + len);
        rx_len_ = len;
        rx_rssi_dbm = rssi;
        irq_flags = crc_error ? parts::sx::kIrqCrcErr : parts::sx::kIrqRxDone;
    }
    void signal_tx_done() { irq_flags = parts::sx::kIrqTxDone; }

    // What the world needs to know to be an honest channel: where this radio is
    // tuned, whether it is listening, and what it just put on the air.
    bool take_tx(uint8_t* out, uint8_t& len) {
        if (!tx_pending) return false;
        tx_pending = false;
        len = static_cast<uint8_t>(tx_buf_.size());
        for (size_t i = 0; i < tx_buf_.size(); i++) out[i] = tx_buf_[i];
        return true;
    }

    bool saw_cmd(uint8_t opcode) const {
        for (uint8_t c : cmds_seen)
            if (c == opcode) return true;
        return false;
    }

    int busy_pin{0}, reset_pin{1}, dio1_pin{2};
    bool busy_stuck{false};
    uint32_t freq_hz{0};
    bool receiving{false};
    bool tx_pending{false};
    // The carrier the radio would measure on the tuned channel right now, and
    // the level the last delivered frame arrived with.
    int8_t rssi_dbm{-110};
    int8_t rx_rssi_dbm{-80};
    uint16_t irq_flags{0};
    int reset_pulses{0};
    std::vector<uint8_t> cmds_seen;

   private:
    uint8_t process(uint8_t in) {
        if (seq_ == 0) {
            opcode_ = in;
            cmds_seen.push_back(in);
            if (opcode_ == parts::sx::kClearIrqStatus) irq_flags = 0;
            if (opcode_ == parts::sx::kSetRx) receiving = true;
            if (opcode_ == parts::sx::kSetTx) {
                receiving = false;
                tx_pending = true;
            }
            if (opcode_ == parts::sx::kWriteBuffer) tx_buf_.clear();
            if (opcode_ == parts::sx::kSetRfFrequency) frf_ = 0;
            return 0;
        }
        if (opcode_ == parts::sx::kWriteBuffer && seq_ >= 2) {
            tx_buf_.push_back(in);
            return 0;
        }
        if (opcode_ == parts::sx::kSetRfFrequency) {
            frf_ = (frf_ << 8) | in;
            // The inverse of the driver's PLL word: frf = freq << 25 / 32 MHz.
            if (seq_ == 4) freq_hz = static_cast<uint32_t>((frf_ * 32000000ULL) >> 25);
            return 0;
        }
        switch (opcode_) {
            case parts::sx::kGetIrqStatus:
                if (seq_ == 2) return static_cast<uint8_t>(irq_flags >> 8);
                if (seq_ == 3) return static_cast<uint8_t>(irq_flags);
                return 0;
            case parts::sx::kGetRxBufferStatus:
                if (seq_ == 2) return rx_len_;
                if (seq_ == 3) return 0;
                return 0;
            case parts::sx::kGetRssiInst:
                if (seq_ == 2) return static_cast<uint8_t>(-2 * rssi_dbm);
                return 0;
            case parts::sx::kGetPacketStatus:
                if (seq_ == 4) return static_cast<uint8_t>(-2 * rx_rssi_dbm);
                return 0;
            case parts::sx::kReadBuffer: {
                if (seq_ >= 3) {
                    size_t idx = seq_ - 3;
                    return idx < rx_buf_.size() ? rx_buf_[idx] : 0;
                }
                return 0;
            }
            default: return 0;
        }
    }

    size_t seq_{0};
    uint8_t opcode_{0xFF};
    uint64_t frf_{0};
    std::vector<uint8_t> rx_buf_;
    std::vector<uint8_t> tx_buf_;
    uint8_t rx_len_{0};
};

}

#endif
