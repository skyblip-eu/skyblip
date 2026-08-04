#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_POWER_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_POWER_H

#include "core/power/battery.h"
#include "core/power/cutoff.h"
#include "hal/capabilities.h"
#include "hal/die_temperature.h"
#include "runtime/service.h"

namespace skyblip::go {

// Owns state.battery and state.power_level: what the divider read becomes the
// voltage and the state of charge every screen and the companion link report,
// and what core/power's cutoff rule made of the same samples. The level is
// published rather than re-derived downstream, so the one place that knows the
// cell is nearly gone is the one place that says so.
class PowerService : public runtime::Service {
   public:
    using runtime::Service::Service;

    void tick(uint32_t now_ms) override;

    bool cutoff() const { return cutoff_.cutoff(); }
    uint32_t implausible_samples() const { return cutoff_.implausible(); }

    // The power-failure comparator fired. Polled off hal::SystemPower by the
    // product and handed here, because this service owns the cutoff monitor and
    // the monitor is where the rule lives (core/power/cutoff.h).
    void on_supply_warning() { cutoff_.on_supply_warning(); }
    bool supply_warned() const { return cutoff_.supply_warned(); }
    uint32_t supply_warnings() const { return cutoff_.supply_warnings(); }

    // Whether a durable write of this kind may happen now. One reader today, the
    // settings writer; the flight log's answer is in the same rule and is always
    // yes, which is the point of asking through it rather than around it.
    bool may_write(power::DurableWrite kind) const { return cutoff_.may_write(kind); }

    // The board's die sensor, wired by the product. Thermals live here because
    // this is the service that already samples the board once a second and
    // because a hot cell and a hot die are one story: the two numbers a support
    // case reads together should not come from two places.
    void attach_die_temperature(hal::DieTemperature& sensor) { die_ = &sensor; }

    // Tenths of a degree, and whether anyone has read one. False on a board with
    // no sensor for ever, which is what the reply reads to decide whether the key
    // exists at all - never a zero standing in for absent, because 0.0 C is a
    // plausible hangar morning.
    bool die_temperature_valid() const { return die_valid_; }
    int16_t die_temperature_dc() const { return die_dc_; }

   private:
    // INFO: fc 05aug26 Die temperature moves in minutes: it is the temperature of
    // a lump of plastic in the sun, low-passed by its own mass. Ten seconds is
    // already generous, so the cheapest correct cadence is the one to take - and
    // the measurement is not free. Zephyr's temp_nrf5 sample_fetch takes a mutex,
    // requests the HFCLK through onoff and then BLOCKS on the DATARDY semaphore
    // (drivers/sensor/nordic/temp/temp_nrf5.c:41-75), so it is a sleeping call and
    // may not be made from an interrupt or from the radio thread. It runs on the
    // service pass, where the executor that owns the dwells outranks it.
    static constexpr uint32_t kDiePeriodMs = 10000;

    void sample_die_temperature(uint32_t now_ms);

    power::Gauge gauge_{};
    power::CutoffMonitor cutoff_{};
    // Never null: the port this starts on is the absent part, so a product that
    // wired nothing reads "no sensor" rather than dereferencing nothing.
    hal::DieTemperature absent_die_{};
    hal::DieTemperature* die_{&absent_die_};
    uint32_t die_read_ms_{0};
    int16_t die_dc_{0};
    bool die_valid_{false};
    bool die_sampled_{false};
};

}  // namespace skyblip::go

#endif
