// devices/models/bhi260ap.h — a model of the BHI260AP at the io::I2c seam, so the
// REAL driver runs against it unchanged.
//
// What this is an oracle for, honestly: the BOOT SEQUENCE and the FIFO framing —
// that the driver resets, waits for the host interface, sends the upload command
// with a length in WORDS, streams the whole image, waits for verify, boots, and
// then refuses framework commands until it has. Those are our logic and this
// pins them.
//
// What it is NOT an oracle for: the register numbers and bit positions. Both the
// model and the driver read them from the same datasheet, so a transcription
// error is common-mode and would pass (3-ARCHITECTURE §8). Only silicon settles
// those. The model therefore rejects anything it does not recognise instead of
// answering 0, so a wrong register shows up as a failure here rather than as
// plausible-looking data.
#ifndef SKYBLIP_DEVICES_MODELS_BHI260AP_H
#define SKYBLIP_DEVICES_MODELS_BHI260AP_H

#include <cstring>
#include <vector>

#include "devices/drivers/bhi260ap.h"
#include "devices/io/io.h"

namespace skyblip::models {

class Bhi260ap : public io::I2c {
   public:
    static constexpr uint8_t kAddr = 0x28;

    // ---- what the modelled part is ------------------------------------------
    uint8_t product_id{drivers::bhi::kProductId};
    uint8_t chip_id{drivers::bhi::kChipId};
    bool on_bus{true};  // false models an empty socket: every transfer NAKs

    // ---- fault injection ----------------------------------------------------
    bool fail_verify{false};  // firmware arrives corrupt
    bool never_ready{false};  // host interface never comes up

    // ---- what the driver did ------------------------------------------------
    int reset_count{0};
    uint32_t upload_words_declared{0};
    std::vector<uint8_t> uploaded;
    bool boot_ram_commanded{false};
    bool running{false};
    std::vector<uint8_t> configured_sensors;

    // Queue a FIFO packet the driver will drain: 1-byte sensor id + payload.
    void queue_sample(uint8_t sensor_id, int16_t x, int16_t y, int16_t z) {
        fifo_.push_back(sensor_id);
        push16(x);
        push16(y);
        push16(z);
    }
    void queue_raw(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; i++) fifo_.push_back(data[i]);
    }

    // ---- io::I2c ------------------------------------------------------------
    bool write(uint8_t addr, const uint8_t* data, size_t len) override {
        if (!on_bus || addr != kAddr || len < 1) return false;
        const uint8_t reg = data[0];
        const uint8_t* payload = data + 1;
        const size_t n = len - 1;

        switch (reg) {
            case drivers::bhi::kRegResetRequest:
                reset_count++;
                running = false;
                uploaded.clear();
                upload_words_declared = 0;
                boot_ram_commanded = false;
                return true;
            case drivers::bhi::kRegHostIrqCtrl:
            case drivers::bhi::kRegChipCtrl: return true;
            case drivers::bhi::kRegCommandInput: return on_command(payload, n);
            default: return false;  // an unmodelled register is a driver bug
        }
    }

    bool read(uint8_t, uint8_t*, size_t) override { return false; }  // driver never uses it

    bool write_read(uint8_t addr, const uint8_t* tx, size_t tx_len, uint8_t* rx,
                    size_t rx_len) override {
        if (!on_bus || addr != kAddr || tx_len != 1) return false;
        switch (tx[0]) {
            case drivers::bhi::kRegProductId:
                if (rx_len != 1) return false;
                rx[0] = product_id;
                return true;
            case drivers::bhi::kRegChipId:
                if (rx_len != 1) return false;
                rx[0] = chip_id;
                return true;
            case drivers::bhi::kRegBootStatus:
                if (rx_len != 1) return false;
                rx[0] = boot_status();
                return true;
            case drivers::bhi::kRegFifoNonWakeLen:
                if (rx_len != 2) return false;
                rx[0] = static_cast<uint8_t>(fifo_.size() & 0xFF);
                rx[1] = static_cast<uint8_t>((fifo_.size() >> 8) & 0xFF);
                return true;
            case drivers::bhi::kRegFifoNonWake: return drain(rx, rx_len);
            default: return false;
        }
    }

   private:
    void push16(int16_t v) {
        fifo_.push_back(static_cast<uint8_t>(static_cast<uint16_t>(v) & 0xFF));
        fifo_.push_back(static_cast<uint8_t>(static_cast<uint16_t>(v) >> 8));
    }

    uint8_t boot_status() const {
        if (never_ready) return 0;
        uint8_t s = drivers::bhi::kBootHostInterfaceReady;
        if (upload_complete()) {
            s |= fail_verify ? drivers::bhi::kBootFwVerifyError : drivers::bhi::kBootFwVerifyDone;
        }
        return s;
    }

    bool upload_complete() const {
        return upload_words_declared != 0 && uploaded.size() == upload_words_declared * 4u;
    }

    bool on_command(const uint8_t* p, size_t n) {
        // Mid-upload, everything written to the command channel is image body.
        if (upload_words_declared != 0 && !upload_complete()) {
            uploaded.insert(uploaded.end(), p, p + n);
            return true;
        }
        if (n < 2) return false;
        const uint16_t cmd = static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1] << 8);
        switch (cmd) {
            case drivers::bhi::kCmdUploadToProgramRam:
                if (n < 4) return false;
                upload_words_declared =
                    static_cast<uint32_t>(p[2]) | static_cast<uint32_t>(p[3] << 8);
                uploaded.clear();
                return true;
            case drivers::bhi::kCmdBootProgramRam:
                boot_ram_commanded = true;
                // A hub whose image failed verification does not start.
                running = upload_complete() && !fail_verify;
                return true;
            case drivers::bhi::kCmdConfigureSensor:
                // Framework command: unavailable in the bootloader (Table 31).
                if (!running || n < 3) return false;
                configured_sensors.push_back(p[2]);
                return true;
            default: return false;
        }
    }

    bool drain(uint8_t* rx, size_t rx_len) {
        if (fifo_.size() < rx_len) return false;
        std::memcpy(rx, fifo_.data(), rx_len);
        fifo_.erase(fifo_.begin(), fifo_.begin() + static_cast<long>(rx_len));
        return true;
    }

    std::vector<uint8_t> fifo_;
};

}  // namespace skyblip::models

#endif
