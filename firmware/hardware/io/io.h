#ifndef SKYBLIP_HARDWARE_IO_IO_H
#define SKYBLIP_HARDWARE_IO_IO_H

#include <cstddef>
#include <cstdint>

namespace skyblip::io {

enum class BusId : uint8_t { Radio, Epd, Gnss, Sensor };

enum class PinFn : uint8_t {
    RadioBusy,
    RadioReset,
    RadioIrq,
    EpdDc,
    EpdReset,
    EpdBusy,
    EpdBacklight,
};

struct PinRole {
    PinFn fn;
    int pin;
};

// What a board tells its platform about its own wiring. The silicon platform
// ignores it (the pins are already in the devicetree and in the drivers' own
// arguments); the host platform uses it to route a pin to the part model that
// owns it.
struct PinMap {
    const PinRole* roles;
    int count;
};

class Gpio {
   public:
    virtual ~Gpio() = default;
    virtual void set(int pin, bool level) = 0;
    virtual bool get(int pin) = 0;
    virtual void mode_output(int pin) = 0;
    virtual void mode_input(int pin, bool pullup) = 0;
};

class Spi {
   public:
    virtual ~Spi() = default;
    virtual void transfer(const uint8_t* tx, uint8_t* rx, size_t len) = 0;
    virtual void select(bool on) = 0;
};

class I2c {
   public:
    virtual ~I2c() = default;
    virtual bool write(uint8_t addr, const uint8_t* data, size_t len) = 0;
    virtual bool read(uint8_t addr, uint8_t* data, size_t len) = 0;
};

class Uart {
   public:
    virtual ~Uart() = default;
    virtual size_t write(const uint8_t* data, size_t len) = 0;
    virtual size_t read(uint8_t* data, size_t cap) = 0;
    virtual size_t available() = 0;
};

}  // namespace skyblip::io

#endif
