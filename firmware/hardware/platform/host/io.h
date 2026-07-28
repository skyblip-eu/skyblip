#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_IO_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_IO_H

#include "hardware/io/io.h"
#include "hardware/parts/l76k/model.h"
#include "hardware/parts/ssd1681/model.h"
#include "hardware/parts/sx1262/model.h"

namespace skyblip::platform::host {

struct Chips {
    models::Sx1262 radio;
    models::Ssd1681 epd;
    models::L76k gnss;
};

// The virtual wiring. A board hands over its pin map once; from then on a pin
// number reaches the part model that owns it, exactly as a trace would.
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
