#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_PLATFORM_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_PLATFORM_H

#include "hal/capabilities.h"
#include "hal/dfu.h"
#include "hal/die_temperature.h"
#include "hardware/parts/bme280/model.h"
#include "hardware/parts/ssd1681/panel.h"
#include "hardware/platform/host/annunciator.h"
#include "hardware/platform/host/clock.h"
#include "hardware/platform/host/flash_region.h"
#include "hardware/platform/host/indicator.h"
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
        hal::Capability::Battery | hal::Capability::Indicator;

    // A host board can be fitted with less than everything, which is how the
    // degraded paths get exercised without a soldering iron.
    explicit Platform(hal::Capabilities fitted = kFullyFitted) : fitted_(fitted) {
        baro_.present = hal::has(fitted, hal::Capability::Baro);
        battery_.present = hal::has(fitted, hal::Capability::Battery);
        log_flash_.set_present(hal::has(fitted, hal::Capability::Storage));
        wire_i2c();
    }

    Status begin() { return Status::Ok; }
    void wire(const io::PinMap& map) { gpio_.wire(map); }

    io::Spi& spi(io::BusId id) {
        if (id == io::BusId::Epd) return chips_.epd;
        return chips_.radio;
    }
    io::Uart& uart(io::BusId) { return chips_.gnss; }
    // Deliberately the null port, even though chips_.gnss can retune: this
    // platform is a bench, not a T-Echo, and the product rig feeds fixes onto the
    // bus rather than through the receiver model. A rate port here would have the
    // driver walk its candidates against a silence that is the rig's, not a
    // receiver's. Autobaud is proven where the model IS the wire,
    // test/hardware/test_l76k.cpp.
    io::UartRate& uart_rate(io::BusId) { return io::kFixedUartRate; }
    io::Gpio& gpio() { return gpio_; }
    io::I2c& i2c(io::BusId) { return i2c_; }

    host::Clock& clock() { return clock_; }
    host::Link& link() { return link_; }
    host::KvStore& kv() { return kv_; }
    host::FlashRegion& log_flash() { return log_flash_; }
    host::Annunciator& annunciator() { return annunciator_; }
    host::Indicator& indicator() { return indicator_; }
    host::Dfu& dfu() { return dfu_; }
    host::Baro& baro() { return baro_; }
    host::Battery& battery() { return battery_; }
    // There is no die sensor here and there is no model of one: hal's port IS the
    // absent part, so this hands back the port that answers "no reading" for
    // ever. It exists because the product is one piece of code over both
    // platforms (1-ARCHITECTURE.md §3.4) - the alternative was a compile-time
    // branch on the platform, which is the thing that invariant forbids. What a
    // host build proves is the absent case: no capability, no reading, no key in
    // the reply.
    hal::DieTemperature& die_temperature() { return die_temperature_; }
    host::Pps& pps() { return pps_; }
    host::Watchdog& watchdog() { return watchdog_; }
    host::SystemPower& system_power() { return system_power_; }
    bool button_down() { return gpio_.button_down; }

    // The panel fingerprint, as the silicon platform's board port takes it: 11
    // bytes of register 0x2D then 10 of 0x2E. The virtual glass carries which lot
    // it is from, so a host test flies a panel that cannot be powered off after a
    // partial update without a soldering iron.
    bool read_panel_signature(parts::PanelSignature& out) {
        out = chips_.epd.signature;
        return out.read;
    }

    // What the buzzer pin answered when it was read high-Z and then against the
    // internal pull-up, before any driver owned it. A pin held low cannot swing a
    // transducer; see the silicon platform for what the reading can and cannot
    // conclude.
    bool buzzer_pin_held_low() const { return buzzer_pin_held_low_; }
    void set_buzzer_pin_held_low(bool held) { buzzer_pin_held_low_ = held; }
    bool read_pressure_pa(uint32_t& out_pa) { return baro_.read_pressure_pa(out_pa); }
    bool read_battery_mv(uint16_t& out_mv) { return battery_.read_mv(out_mv); }
    bool external_power() { return battery_.external_power; }
    uint32_t device_addr() const { return 0x0ABBCC; }
    Chips& chips() { return chips_; }
    Gpio& board_gpio() { return gpio_; }
    // The bus itself, so a test can fit a unit that came off the line with the
    // other barometer address, or with a part nobody expected.
    I2cBus& i2c_bus() { return i2c_; }

    hal::Capabilities capabilities() const { return fitted_; }

   private:
    // Which addresses the virtual bus acknowledges, from what the board is fitted
    // with. The barometer answers at 0x76 rather than 0x77 - our BOM's address,
    // one of the two the devicetree declares - and the two parts this product
    // deliberately does not drive answer as well, because they are soldered on and
    // a scan that hid them would be a scan nobody could trust.
    void wire_i2c() {
        if (hal::has(fitted_, hal::Capability::Baro)) i2c_.answer(kBaroAddress, true);
        if (hal::has(fitted_, hal::Capability::Vibro))
            i2c_.attach(models::Drv2605::kAddress, chips_.haptic);
        i2c_.answer(kImuAddress, true);
        i2c_.answer(kRtcAddress, true);
    }

    // BME280 at our BOM's address, BHI260AP, PCF8563. The last two have no
    // driver anywhere in the tree, by decision (project/2-DEVICES.md).
    static constexpr uint8_t kBaroAddress = 0x76;
    static constexpr uint8_t kImuAddress = 0x28;
    static constexpr uint8_t kRtcAddress = 0x51;

    Chips chips_{};
    Gpio gpio_{chips_};
    I2cBus i2c_{};
    host::Clock clock_{};
    host::Link link_{};
    host::KvStore kv_{};
    host::FlashRegion log_flash_{};
    host::Annunciator annunciator_{};
    host::Indicator indicator_{};
    host::Dfu dfu_{};
    host::Baro baro_{};
    host::Battery battery_{};
    hal::DieTemperature die_temperature_{};
    host::Pps pps_{clock_};
    host::Watchdog watchdog_{};
    host::SystemPower system_power_{};
    hal::Capabilities fitted_;
    bool buzzer_pin_held_low_{false};
};

}  // namespace skyblip::platform::host

#endif
