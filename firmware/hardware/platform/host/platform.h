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
#include "hardware/platform/host/system_power.h"
#include "hardware/platform/host/watchdog.h"

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

// The cell the world charges and drains. Millivolts is the only thing the board
// can read on silicon, so it is the only thing settable here.
class Battery {
   public:
    bool ready() const { return present; }
    bool read_mv(uint16_t& out_mv) {
        if (!present) return false;
        out_mv = millivolts;
        return true;
    }

    bool present{true};
    uint16_t millivolts{4050};
    bool external_power{false};
};

// The same PPS surface the silicon platform offers: a phase, and the instant the
// edge itself arrived. The modelled receiver pulses on the whole second of the
// clock the caller advances, so the edge is that second - not the phase rounded
// to the millisecond whoever asked happened to ask on.
class Pps {
   public:
    explicit Pps(const Clock& clock) : clock_(clock) {}

    bool locked() const { return locked_; }
    void set_locked(bool on) { locked_ = on; }
    uint32_t ms_since(uint64_t now_us) const {
        return locked_ ? static_cast<uint32_t>(now_us / 1000 % 1000) : 0;
    }

    uint64_t last_edge_us() const {
        if (!locked_) return 0;
        const uint64_t now_us = clock_.micros();
        return now_us - now_us % kSecondUs;
    }

   private:
    static constexpr uint64_t kSecondUs = 1000000;

    const Clock& clock_;
    bool locked_{true};
};

// The host platform: the same role surface the silicon platform offers, backed
// by part models and a clock the caller advances. A board cannot tell them apart.
class Platform {
   public:
    using Rf = host::Rf;
    using Link = host::Link;

    static constexpr hal::Capabilities kFullyFitted =
        hal::Capability::Display | hal::Capability::Gnss | hal::Capability::Baro |
        hal::Capability::Link | hal::Capability::Storage | hal::Capability::Dfu |
        hal::Capability::Buzzer | hal::Capability::Vibro | hal::Capability::Button |
        hal::Capability::Battery;

    // A host board can be fitted with less than everything, which is how the
    // degraded paths get exercised without a soldering iron.
    explicit Platform(hal::Capabilities fitted = kFullyFitted) : fitted_(fitted) {
        baro_.present = hal::has(fitted, hal::Capability::Baro);
        battery_.present = hal::has(fitted, hal::Capability::Battery);
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
    host::Battery& battery() { return battery_; }
    host::Pps& pps() { return pps_; }
    host::Watchdog& watchdog() { return watchdog_; }
    host::SystemPower& system_power() { return system_power_; }
    bool button_down() { return gpio_.button_down; }
    bool read_pressure_pa(uint32_t& out_pa) { return baro_.read_pressure_pa(out_pa); }
    bool read_battery_mv(uint16_t& out_mv) { return battery_.read_mv(out_mv); }
    bool external_power() { return battery_.external_power; }
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
    host::Battery battery_{};
    host::Pps pps_{clock_};
    host::Watchdog watchdog_{};
    host::SystemPower system_power_{};
    hal::Capabilities fitted_;
};

}  // namespace skyblip::platform::host

#endif
