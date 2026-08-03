// hardware/parts/sx1262/model.h: a behavioural SX1262 model at the SPI/GPIO seam, so
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
    // What this chip refuses to do, and why. The model is the datasheet's half
    // of the contract: the driver either honours it or lands here.
    enum class Fault : uint8_t {
        None,
        ShortReset,
        ConfigOutsideStandby,
        TxWithoutPower,
        FrameWithoutModulation,
        ImageCalibrationTooEarly,
    };

    void set(int pin, bool level) override {
        if (pin != reset_pin) return;
        if (!level) {
            reset_low = true;
            reset_low_spins = 0;
            return;
        }
        if (!reset_low) return;
        reset_low = false;
        if (reset_low_spins < parts::sx::kResetLowSpins) note_fault(Fault::ShortReset);
        reset_pulses++;
        power_on_reset();
    }
    bool get(int pin) override {
        if (pin == busy_pin) {
            if (reset_low) reset_low_spins++;
            return busy_stuck;
        }
        if (pin == dio1_pin) return (irq_flags & dio1_mask) != 0;
        return false;
    }
    void mode_output(int) override {}
    void mode_input(int, bool) override {}

    void select(bool on) override {
        if (on) {
            // DS 9.3: the falling edge on NSS is the wake-up. Whatever the host
            // meant to send, the first thing it does is bring the part back.
            if (sleeping) {
                sleeping = false;
                standby = true;
                wakes++;
            }
            seq_ = 0;
            opcode_ = 0xFF;
        }
    }
    // A part that is not fitted, or whose MISO pad never got soldered, still
    // lets the host clock bytes out and still lets BUSY be read low.
    void transfer(const uint8_t* tx, uint8_t* rx, size_t len) override {
        for (size_t i = 0; i < len; i++) {
            uint8_t in = tx ? tx[i] : 0;
            uint8_t out = process(in);
            if (rx) rx[i] = miso_dead ? miso_level : out;
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
        // A modem still on its reset defaults does not frame 100 kbps GFSK.
        if (!modulation_set) return false;
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

    void set_rssi_sequence(const int8_t* levels, uint8_t n) {
        rssi_sequence_len = n < kRssiSequenceCap ? n : kRssiSequenceCap;
        rssi_sequence_at = 0;
        for (uint8_t i = 0; i < rssi_sequence_len; i++) rssi_sequence[i] = levels[i];
    }

    // The chip's own SetTx timeout running out. A SetTx issued with timeout 0
    // has nothing to run out, which is the PA left keyed for ever.
    bool expire_tx() {
        if (tx_timeout_ticks == 0) return false;
        irq_flags = parts::sx::kIrqTimeout;
        return true;
    }

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
    bool miso_dead{false};
    uint8_t miso_level{0x00};
    bool sleeping{false};
    uint8_t sleep_config{0xFF};
    uint32_t wakes{0};
    uint32_t freq_hz{0};
    bool receiving{false};
    bool tx_pending{false};
    // The carrier the radio would measure on the tuned channel right now, and
    // the level the last delivered frame arrived with.
    int8_t rssi_dbm{-110};
    int8_t rx_rssi_dbm{-80};
    // A clear-channel assessment is a run of GetRssiInst reads over an interval,
    // so the chip has to be able to answer them differently: a level sequence,
    // walked one entry per read and repeating, standing in for a channel that
    // changes inside the window. Empty, every read gets rssi_dbm.
    static constexpr uint8_t kRssiSequenceCap = 16;
    int8_t rssi_sequence[kRssiSequenceCap]{};
    uint8_t rssi_sequence_len{0};
    uint8_t rssi_sequence_at{0};
    uint8_t sync[8]{};
    uint8_t sync_bits{0};
    uint8_t payload_bytes{0};
    uint16_t irq_flags{0};
    int reset_pulses{0};
    bool reset_low{false};
    uint32_t reset_low_spins{0};
    bool standby{false};
    bool tcxo_powered{false};
    bool calibrated{false};
    bool image_calibrated{false};
    uint8_t image_band[2]{};
    uint8_t modulation[8]{};
    bool modulation_set{false};
    uint32_t bitrate{0};
    uint32_t fdev_hz{0};
    uint8_t pulse_shape{0xFF};
    uint8_t rx_bandwidth{0xFF};
    uint8_t pa_config[4]{};
    bool pa_set{false};
    bool tx_power_set{false};
    int8_t tx_power_dbm{0};
    uint8_t ramp_time{0xFF};
    uint16_t irq_mask{0};
    uint16_t dio1_mask{0};
    uint32_t tx_timeout_ticks{0};
    Fault fault{Fault::None};
    uint32_t faults{0};
    std::vector<uint8_t> cmds_seen;

    // Where an opcode sits in the order it was actually issued, -1 if never.
    int cmd_order(uint8_t opcode) const {
        for (size_t i = 0; i < cmds_seen.size(); i++)
            if (cmds_seen[i] == opcode) return static_cast<int>(i);
        return -1;
    }

   private:
    static bool bit_at(const uint8_t* bytes, int index) {
        return ((bytes[index >> 3] >> (7 - (index & 7))) & 1u) != 0;
    }

    int8_t next_rssi_dbm() {
        if (rssi_sequence_len == 0) return rssi_dbm;
        const int8_t level = rssi_sequence[rssi_sequence_at];
        rssi_sequence_at = static_cast<uint8_t>((rssi_sequence_at + 1) % rssi_sequence_len);
        return level;
    }

    void note_fault(Fault f) {
        if (fault == Fault::None) fault = f;
        faults++;
    }

    // NRESET returns every block to its reset default, including the IRQ mask
    // and the modem: everything the driver programmed is gone.
    void power_on_reset() {
        standby = true;
        sleeping = false;
        tcxo_powered = calibrated = image_calibrated = false;
        modulation_set = pa_set = tx_power_set = false;
        irq_mask = dio1_mask = 0;
        irq_flags = 0;
        receiving = false;
        tx_timeout_ticks = 0;
    }

    // DS 13.1: these are accepted in standby only. Issued on a chip that is
    // still receiving, they are silently dropped on silicon.
    static bool standby_only(uint8_t opcode) {
        return opcode == parts::sx::kSetPacketType || opcode == parts::sx::kSetRfFrequency ||
               opcode == parts::sx::kSetModulationParams || opcode == parts::sx::kSetPacketParams ||
               opcode == parts::sx::kSetPaConfig || opcode == parts::sx::kSetTxParams ||
               opcode == parts::sx::kWriteRegister || opcode == parts::sx::kCalibrate ||
               opcode == parts::sx::kCalibrateImage;
    }

    uint8_t process(uint8_t in) {
        if (seq_ == 0) {
            opcode_ = in;
            cmds_seen.push_back(in);
            if (standby_only(in) && !standby) note_fault(Fault::ConfigOutsideStandby);
            if (opcode_ == parts::sx::kClearIrqStatus) irq_flags = 0;
            if (opcode_ == parts::sx::kSetStandby) standby = true;
            if (opcode_ == parts::sx::kSetSleep) {
                sleeping = true;
                standby = false;
                receiving = false;
            }
            if (opcode_ == parts::sx::kSetDio3AsTcxoCtrl) tcxo_powered = true;
            if (opcode_ == parts::sx::kCalibrate) calibrated = true;
            if (opcode_ == parts::sx::kCalibrateImage) {
                if (!tcxo_powered || !calibrated) note_fault(Fault::ImageCalibrationTooEarly);
                image_calibrated = true;
            }
            if (opcode_ == parts::sx::kSetRx) {
                if (!modulation_set) note_fault(Fault::FrameWithoutModulation);
                receiving = true;
                standby = false;
            }
            if (opcode_ == parts::sx::kSetTx) {
                receiving = false;
                standby = false;
                tx_timeout_ticks = 0;
                if (!tx_power_set) {
                    note_fault(Fault::TxWithoutPower);
                } else {
                    tx_pending = true;
                }
            }
            if (opcode_ == parts::sx::kWriteBuffer) tx_buf_.clear();
            if (opcode_ == parts::sx::kWriteRegister || opcode_ == parts::sx::kReadRegister)
                reg_addr_ = 0;
            if (opcode_ == parts::sx::kSetRfFrequency) frf_ = 0;
            return 0;
        }
        if (opcode_ == parts::sx::kWriteBuffer && seq_ >= 2) {
            tx_buf_.push_back(in);
            return 0;
        }
        if (opcode_ == parts::sx::kSetSleep) {
            if (seq_ == 1) sleep_config = in;
            return 0;
        }
        if (opcode_ == parts::sx::kReadRegister) {
            if (seq_ <= 2) {
                reg_addr_ = static_cast<uint16_t>((reg_addr_ << 8) | in);
                if (seq_ == 2) reg_offset_ = 0;
                return 0;
            }
            if (seq_ == 3) return 0;  // the NOP the chip answers nothing to
            const uint16_t addr = static_cast<uint16_t>(reg_addr_ + reg_offset_++);
            if (addr >= parts::sx::kSyncWordRegister &&
                addr < parts::sx::kSyncWordRegister + sizeof(sync))
                return sync[addr - parts::sx::kSyncWordRegister];
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
        if (opcode_ == parts::sx::kSetModulationParams) {
            if (seq_ >= 1 && seq_ <= 8) modulation[seq_ - 1] = in;
            if (seq_ == 8) {
                const uint32_t br = (static_cast<uint32_t>(modulation[0]) << 16) |
                                    (static_cast<uint32_t>(modulation[1]) << 8) | modulation[2];
                const uint32_t dev = (static_cast<uint32_t>(modulation[5]) << 16) |
                                     (static_cast<uint32_t>(modulation[6]) << 8) | modulation[7];
                bitrate = br ? static_cast<uint32_t>(32ULL * 32000000ULL / br) : 0;
                fdev_hz = static_cast<uint32_t>((static_cast<uint64_t>(dev) * 32000000ULL) >> 25);
                pulse_shape = modulation[3];
                rx_bandwidth = modulation[4];
                modulation_set = br != 0;
            }
            return 0;
        }
        if (opcode_ == parts::sx::kSetPaConfig) {
            if (seq_ >= 1 && seq_ <= 4) pa_config[seq_ - 1] = in;
            if (seq_ == 4) pa_set = true;
            return 0;
        }
        if (opcode_ == parts::sx::kSetTxParams) {
            if (seq_ == 1) tx_power_dbm = static_cast<int8_t>(in);
            if (seq_ == 2) {
                ramp_time = in;
                tx_power_set = pa_set;
            }
            return 0;
        }
        if (opcode_ == parts::sx::kSetDioIrqParams) {
            if (seq_ == 1) irq_mask = in;
            if (seq_ == 2) irq_mask = static_cast<uint16_t>((irq_mask << 8) | in);
            if (seq_ == 3) dio1_mask = in;
            if (seq_ == 4) dio1_mask = static_cast<uint16_t>((dio1_mask << 8) | in);
            return 0;
        }
        if (opcode_ == parts::sx::kCalibrateImage) {
            if (seq_ >= 1 && seq_ <= 2) image_band[seq_ - 1] = in;
            return 0;
        }
        if (opcode_ == parts::sx::kSetTx) {
            if (seq_ >= 1 && seq_ <= 3) tx_timeout_ticks = (tx_timeout_ticks << 8) | in;
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
            // DS 13.3.1: the status register only ever shows unmasked bits.
            case parts::sx::kGetIrqStatus:
                if (seq_ == 2) return static_cast<uint8_t>((irq_flags & irq_mask) >> 8);
                if (seq_ == 3) return static_cast<uint8_t>(irq_flags & irq_mask);
                return 0;
            case parts::sx::kGetRxBufferStatus:
                if (seq_ == 2) return rx_len_;
                if (seq_ == 3) return 0;
                return 0;
            case parts::sx::kGetRssiInst:
                if (seq_ == 2) return static_cast<uint8_t>(-2 * next_rssi_dbm());
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
