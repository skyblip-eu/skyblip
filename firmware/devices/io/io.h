// devices/io/io.h — low-level bus contracts (spi/i2c/uart/gpio): the driver<->soc
#ifndef SKYBLIP_DEVICES_IO_IO_H
#define SKYBLIP_DEVICES_IO_IO_H

#include <cstddef>
#include <cstdint>

namespace skyblip::io {

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
    // Register read as ONE transaction with a repeated start. Not write() then
    // read(): a STOP in between lets another master interleave, and some parts
    // reset their register pointer on it.
    virtual bool write_read(uint8_t addr, const uint8_t* tx, size_t tx_len, uint8_t* rx,
                            size_t rx_len) = 0;
};

class Uart {
   public:
    virtual ~Uart() = default;
    virtual size_t write(const uint8_t* data, size_t len) = 0;
    virtual size_t read(uint8_t* data, size_t cap) = 0;
    virtual size_t available() = 0;
};

}

#endif
