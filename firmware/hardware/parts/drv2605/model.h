#ifndef SKYBLIP_HARDWARE_MODEL_DRV2605_H
#define SKYBLIP_HARDWARE_MODEL_DRV2605_H

#include <cstdint>

#include "hardware/io/io.h"

namespace skyblip::models {

// The virtual DRV2605, at the two seams the real one has: an I2C register file
// and an enable pin. The important behaviour it models is the one that broke us:
// the enable pin does not move the motor. drive() is zero until the part has
// been taken out of standby, put in a playing mode and given something to play,
// which is exactly the sequence a GPIO and a timer cannot perform.
class Drv2605 : public io::I2c, public io::Gpio {
   public:
    static constexpr uint8_t kAddress = 0x5A;
    static constexpr uint8_t kRegisterCount = 0x24;

    // DRV2605L: STATUS[7:5] = 7. Settable, so a test can present a part that
    // answers the address but is not a haptic driver at all.
    Drv2605() { registers[0x00] = 7 << 5; }

    // io::I2c. A register write is address then value, a register read is a
    // one-byte write of the address followed by a read, which is what the driver
    // does and what the datasheet's protocol section describes.
    bool write(uint8_t addr, const uint8_t* data, size_t len) override {
        if (addr != kAddress || !answers) return false;
        writes++;
        if (len == 0) return true;  // the bus's presence probe
        pointer = data[0];
        for (size_t i = 1; i < len; i++) {
            const uint8_t reg = static_cast<uint8_t>(pointer + i - 1);
            if (reg < kRegisterCount) registers[reg] = data[i];
            if (reg == kRegGo && data[i] != 0) go_pulses++;
        }
        return true;
    }

    bool read(uint8_t addr, uint8_t* data, size_t len) override {
        if (addr != kAddress || !answers) return false;
        reads++;
        for (size_t i = 0; i < len; i++) {
            const uint8_t reg = static_cast<uint8_t>(pointer + i);
            data[i] = reg < kRegisterCount ? registers[reg] : 0xFF;
        }
        return true;
    }

    // io::Gpio: the enable line, and nothing else on this part.
    void set(int pin, bool level) override {
        if (pin != enable_pin) return;
        if (level && !enabled) enable_raises++;
        enabled = level;
    }
    bool get(int pin) override { return pin == enable_pin ? enabled : false; }
    void mode_output(int) override {}
    void mode_input(int pin, bool) override {
        if (pin == enable_pin) enabled = false;
    }

    uint8_t mode() const { return registers[kRegMode] & 0x07; }
    bool standby() const { return (registers[kRegMode] & kStandby) != 0; }
    uint8_t library() const { return registers[kRegLibrary] & 0x07; }
    bool erm_selected() const { return (registers[kRegFeedback] & 0x80) == 0; }
    bool open_loop() const { return (registers[kRegControl3] & 0x20) != 0; }

    // What the actuator is actually being driven with, 0 for nothing. Real-time
    // playback holds its value; a waveform sequence only moves while GO is set.
    uint8_t drive() const {
        if (standby()) return 0;
        if (mode() == kModeRealTimePlayback) return registers[kRegRtpInput];
        if (registers[kRegGo] != 0) return registers[kRegWaveSeq1] != 0 ? 0xFF : 0;
        return 0;
    }
    bool moving() const { return drive() != 0; }

    uint8_t registers[kRegisterCount]{};
    uint8_t pointer{0};
    int enable_pin{8};
    bool enabled{false};
    bool answers{true};
    int enable_raises{0};
    int go_pulses{0};
    int writes{0};
    int reads{0};

   private:
    static constexpr uint8_t kRegMode = 0x01;
    static constexpr uint8_t kRegRtpInput = 0x02;
    static constexpr uint8_t kRegLibrary = 0x03;
    static constexpr uint8_t kRegWaveSeq1 = 0x04;
    static constexpr uint8_t kRegGo = 0x0C;
    static constexpr uint8_t kRegFeedback = 0x1A;
    static constexpr uint8_t kRegControl3 = 0x1D;
    static constexpr uint8_t kStandby = 0x40;
    static constexpr uint8_t kModeRealTimePlayback = 0x05;
};

}  // namespace skyblip::models

#endif
