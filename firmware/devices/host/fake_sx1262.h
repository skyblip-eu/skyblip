// devices/host/fake_sx1262.h — a BEHAVIORAL SX1262 model (not a byte-recorder):
#ifndef SKYBLIP_DEVICES_HOST_FAKE_SX1262_H
#define SKYBLIP_DEVICES_HOST_FAKE_SX1262_H

#include <cstring>
#include <vector>

#include "devices/drivers/sx1262.h"
#include "devices/io/io.h"

namespace skyblip::host {

class FakeSx1262 : public io::Spi, public io::Gpio {
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

    void queue_rx(const uint8_t* data, uint8_t len, bool crc_error = false) {
        rx_buf_.assign(data, data + len);
        rx_len_ = len;
        irq_flags = crc_error ? drivers::sx::kIrqCrcErr : drivers::sx::kIrqRxDone;
    }
    void signal_tx_done() { irq_flags = drivers::sx::kIrqTxDone; }

    bool saw_cmd(uint8_t opcode) const {
        for (uint8_t c : cmds_seen)
            if (c == opcode) return true;
        return false;
    }

    int busy_pin{0}, reset_pin{1}, dio1_pin{2};
    bool busy_stuck{false};
    uint16_t irq_flags{0};
    int reset_pulses{0};
    std::vector<uint8_t> cmds_seen;

   private:
    uint8_t process(uint8_t in) {
        if (seq_ == 0) {
            opcode_ = in;
            cmds_seen.push_back(in);
            if (opcode_ == drivers::sx::kClearIrqStatus) irq_flags = 0;
            return 0;
        }
        switch (opcode_) {
            case drivers::sx::kGetIrqStatus:
                if (seq_ == 2) return static_cast<uint8_t>(irq_flags >> 8);
                if (seq_ == 3) return static_cast<uint8_t>(irq_flags);
                return 0;
            case drivers::sx::kGetRxBufferStatus:
                if (seq_ == 2) return rx_len_;
                if (seq_ == 3) return 0;
                return 0;
            case drivers::sx::kReadBuffer: {
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
    std::vector<uint8_t> rx_buf_;
    uint8_t rx_len_{0};
};

}

#endif
