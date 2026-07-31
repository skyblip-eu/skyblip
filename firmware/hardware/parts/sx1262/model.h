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

    // A burst arrives as chips. The detector slides its configured pattern over
    // them, and what the chip reports is the bytes that follow the match: a
    // pattern the transmitter never sent is a frame this radio never saw.
    static uint8_t deliver_after_sync(const uint8_t* chips, uint8_t chip_len, const uint8_t* sync,
                                      uint8_t sync_bits, uint8_t* out, uint8_t cap) {
        const int total = static_cast<int>(chip_len) * 8;
        for (int start = 0; start + sync_bits <= total; start++) {
            bool match = true;
            for (int i = 0; i < sync_bits && match; i++)
                match = bit_at(chips, start + i) == bit_at(sync, i);
            if (!match) continue;
            const int first = start + sync_bits;
            for (uint8_t i = 0; i < cap; i++) {
                uint8_t byte = 0;
                for (int b = 0; b < 8; b++) {
                    const int idx = first + i * 8 + b;
                    const bool set = idx < total && bit_at(chips, idx);
                    byte = static_cast<uint8_t>((byte << 1) | (set ? 1u : 0u));
                }
                out[i] = byte;
            }
            return cap;
        }
        return 0;
    }

    bool receive_air(const uint8_t* chips, uint8_t chip_len, bool crc_error = false,
                     int8_t rssi = -80) {
        if (sync_bits == 0 || payload_bytes == 0) return false;
        uint8_t payload[255];
        const uint8_t n =
            deliver_after_sync(chips, chip_len, sync, sync_bits, payload, payload_bytes);
        if (n == 0) return false;
        queue_rx(payload, n, crc_error, rssi);
        return true;
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
    uint8_t sync[8]{};
    uint8_t sync_bits{0};
    uint8_t payload_bytes{0};
    uint16_t irq_flags{0};
    int reset_pulses{0};
    std::vector<uint8_t> cmds_seen;

   private:
    static bool bit_at(const uint8_t* bytes, int index) {
        return ((bytes[index >> 3] >> (7 - (index & 7))) & 1u) != 0;
    }

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
            if (opcode_ == parts::sx::kWriteRegister) reg_addr_ = 0;
            if (opcode_ == parts::sx::kSetRfFrequency) frf_ = 0;
            return 0;
        }
        if (opcode_ == parts::sx::kWriteBuffer && seq_ >= 2) {
            tx_buf_.push_back(in);
            return 0;
        }
        if (opcode_ == parts::sx::kWriteRegister) {
            if (seq_ <= 2) {
                reg_addr_ = static_cast<uint16_t>((reg_addr_ << 8) | in);
                if (seq_ == 2) reg_offset_ = 0;
                return 0;
            }
            const uint16_t addr = static_cast<uint16_t>(reg_addr_ + reg_offset_++);
            if (addr >= parts::sx::kSyncWordRegister &&
                addr < parts::sx::kSyncWordRegister + sizeof(sync))
                sync[addr - parts::sx::kSyncWordRegister] = in;
            return 0;
        }
        if (opcode_ == parts::sx::kSetPacketParams) {
            if (seq_ == 4) sync_bits = in;
            if (seq_ == 7) payload_bytes = in;
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
    uint16_t reg_addr_{0};
    uint16_t reg_offset_{0};
    uint64_t frf_{0};
    std::vector<uint8_t> rx_buf_;
    std::vector<uint8_t> tx_buf_;
    uint8_t rx_len_{0};
};

}

#endif
