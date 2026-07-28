#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_PLATFORM_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_PLATFORM_H

#include "hal/capabilities.h"
#include "hal/dfu.h"
#include "hardware/parts/bme280/model.h"
#include "hardware/platform/host/annunciator.h"
#include "hardware/platform/host/clock.h"
#include "hardware/platform/host/io.h"
#include "hardware/platform/host/kvstore.h"
#include "hardware/platform/host/link.h"
#include "hardware/platform/host/rf.h"

namespace skyblip::platform::host {

class Dfu : public hal::Dfu {
   public:
    void trigger() override { triggered++; }
    void confirm() override { confirmed++; }
    void enter_recovery() override { recoveries++; }

    int triggered{0};
    int confirmed{0};
    int recoveries{0};
};

class Baro {
   public:
    bool ready() const { return present; }
    bool read_pressure_pa(uint32_t& out_pa) {
        if (!present) return false;
        out_pa = chip.pressure_pa();
        return true;
    }

    models::Bme280 chip;
    bool present{true};
};

class Pps {
   public:
    bool locked() const { return locked_; }
    void set_locked(bool on) { locked_ = on; }
    uint32_t ms_since(uint64_t now_us) const {
        return locked_ ? static_cast<uint32_t>(now_us / 1000 % 1000) : 0;
    }

   private:
    bool locked_{true};
};

// The host platform: the same role surface the silicon platform offers, backed
// by part models and a clock the caller advances. A board cannot tell them apart.
class Platform {
   public:
    using Rf = host::Rf;
    using Link = host::Link;

    static constexpr hal::Capabilities kFullyFitted =
        hal::Capability::Display | hal::Capability::Gnss | hal::Capability::Baro | hal::Capability::Link | hal::Capability::Storage |
        hal::Capability::Dfu | hal::Capability::Buzzer | hal::Capability::Vibro | hal::Capability::Button;

    // A host board can be fitted with less than everything, which is how the
    // degraded paths get exercised without a soldering iron.
    explicit Platform(hal::Capabilities fitted = kFullyFitted) : fitted_(fitted) {
        baro_.present = hal::has(fitted, hal::Capability::Baro);
    }

    Status begin() { return Status::Ok; }
    void wire(const io::PinMap& map) { gpio_.wire(map); }

    io::Spi& spi(io::BusId id) {
        if (id == io::BusId::Epd) return chips_.epd;
        return chips_.radio;
    }
    io::Uart& uart(io::BusId) { return chips_.gnss; }
    io::Gpio& gpio() { return gpio_; }

    host::Clock& clock() { return clock_; }
    host::Link& link() { return link_; }
    host::KvStore& kv() { return kv_; }
    host::Annunciator& annunciator() { return annunciator_; }
    host::Dfu& dfu() { return dfu_; }
    host::Baro& baro() { return baro_; }
    host::Pps& pps() { return pps_; }
    bool button_down() { return gpio_.button_down; }
    bool read_pressure_pa(uint32_t& out_pa) { return baro_.read_pressure_pa(out_pa); }
    uint32_t device_addr() const { return 0x0ABBCC; }
    Chips& chips() { return chips_; }
    Gpio& board_gpio() { return gpio_; }

    hal::Capabilities capabilities() const { return fitted_; }

   private:
    Chips chips_{};
    Gpio gpio_{chips_};
    host::Clock clock_{};
    host::Link link_{};
    host::KvStore kv_{};
    host::Annunciator annunciator_{};
    host::Dfu dfu_{};
    host::Baro baro_{};
    host::Pps pps_{};
    hal::Capabilities fitted_;
};

}  // namespace skyblip::platform::host

#endif
