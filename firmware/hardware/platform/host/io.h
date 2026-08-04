#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_IO_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_IO_H

#include "hardware/io/io.h"
#include "hardware/parts/drv2605/model.h"
#include "hardware/parts/l76k/model.h"
#include "hardware/parts/ssd1681/model.h"
#include "hardware/parts/sx1262/model.h"

namespace skyblip::platform::host {

struct Chips {
    models::Sx1262 radio;
    models::Ssd1681 epd;
    models::L76k gnss;
    models::Drv2605 haptic;
};

// The virtual I2C bus. Two kinds of thing hang off it, which is the truth of
// this board: parts we drive, which get a model, and parts that are fitted and
// deliberately unused - the IMU, the RTC, the touch controller - which answer
// their address and nothing more. A scan has to see both, or the self-test page
// learns less than a bench multimeter.
class I2cBus : public io::I2c {
   public:
    static constexpr int kMaxDevices = 8;

    void attach(uint8_t address, io::I2c& device) {
        if (device_count_ >= kMaxDevices) return;
        devices_[device_count_].address = address;
        devices_[device_count_].device = &device;
        device_count_++;
        answer(address, true);
    }

    // A part that is fitted and mute to us: it acknowledges its address, and any
    // read of it returns nothing.
    void answer(uint8_t address, bool on) {
        if (address >= kAddressSpace) return;
        if (on)
            answers_[address >> 3] |= static_cast<uint8_t>(1u << (address & 7));
        else
            answers_[address >> 3] &= static_cast<uint8_t>(~(1u << (address & 7)));
    }

    bool answers(uint8_t address) const {
        return address < kAddressSpace &&
               (answers_[address >> 3] & static_cast<uint8_t>(1u << (address & 7))) != 0;
    }

    bool write(uint8_t address, const uint8_t* data, size_t len) override {
        if (!answers(address)) return false;
        io::I2c* device = route(address);
        if (device) return device->write(address, data, len);
        return true;  // acknowledged, and nothing listened
    }

    bool read(uint8_t address, uint8_t* data, size_t len) override {
        if (!answers(address)) return false;
        io::I2c* device = route(address);
        if (device) return device->read(address, data, len);
        for (size_t i = 0; i < len; i++) data[i] = 0xFF;
        return true;
    }

   private:
    static constexpr int kAddressSpace = 128;

    struct Slot {
        uint8_t address{0};
        io::I2c* device{nullptr};
    };

    io::I2c* route(uint8_t address) {
        for (int i = 0; i < device_count_; i++)
            if (devices_[i].address == address) return devices_[i].device;
        return nullptr;
    }

    Slot devices_[kMaxDevices]{};
    int device_count_{0};
    uint8_t answers_[kAddressSpace / 8]{};
};

// The virtual wiring. A board hands over its pin map once. From then on a pin
// number reaches the part model that owns it, exactly as a trace would.
//
// The haptic driver's enable line is deliberately not in the map: the modelled
// DRV2605 answers its address whether or not the pin is driven, which is what
// the real part does - SoftRF finds 0x5A at platform/nRF52.cpp:1158, long before
// it drives that pin at 2130. The pin is exercised against the model directly in
// the part's own test, where the model stands in for the trace as well as the bus.
class Gpio : public io::Gpio {
   public:
    explicit Gpio(Chips& chips) : chips_(chips) {}

    void wire(const io::PinMap& map) {
        for (int i = 0; i < map.count; i++) {
            const io::PinRole& r = map.roles[i];
            switch (r.fn) {
                case io::PinFn::RadioBusy: chips_.radio.busy_pin = r.pin; break;
                case io::PinFn::RadioReset: chips_.radio.reset_pin = r.pin; break;
                case io::PinFn::RadioIrq: chips_.radio.dio1_pin = r.pin; break;
                case io::PinFn::EpdDc: chips_.epd.dc = r.pin; break;
                case io::PinFn::EpdReset: chips_.epd.rst = r.pin; break;
                case io::PinFn::EpdBusy: chips_.epd.busy = r.pin; break;
                case io::PinFn::EpdBacklight: chips_.epd.backlight_pin = r.pin; break;
            }
            owner_[i] = r;
            count_ = i + 1;
        }
    }

    void set(int pin, bool level) override {
        io::Gpio* t = route(pin);
        if (t) t->set(pin, level);
    }
    bool get(int pin) override {
        io::Gpio* t = route(pin);
        return t ? t->get(pin) : false;
    }
    void mode_output(int pin) override {
        io::Gpio* t = route(pin);
        if (t) t->mode_output(pin);
    }
    void mode_input(int pin, bool pullup) override {
        io::Gpio* t = route(pin);
        if (t) t->mode_input(pin, pullup);
    }

    bool button_down{false};

   private:
    static constexpr int kMaxPins = 16;

    io::Gpio* route(int pin) {
        for (int i = 0; i < count_; i++) {
            if (owner_[i].pin != pin) continue;
            switch (owner_[i].fn) {
                case io::PinFn::RadioBusy:
                case io::PinFn::RadioReset:
                case io::PinFn::RadioIrq: return &chips_.radio;
                case io::PinFn::EpdDc:
                case io::PinFn::EpdReset:
                case io::PinFn::EpdBusy:
                case io::PinFn::EpdBacklight: return &chips_.epd;
            }
        }
        return nullptr;
    }

    Chips& chips_;
    io::PinRole owner_[kMaxPins]{};
    int count_{0};
};

}  // namespace skyblip::platform::host

#endif
