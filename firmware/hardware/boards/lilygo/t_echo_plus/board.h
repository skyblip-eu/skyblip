#ifndef SKYBLIP_HARDWARE_BOARDS_T_ECHO_PLUS_BOARD_H
#define SKYBLIP_HARDWARE_BOARDS_T_ECHO_PLUS_BOARD_H

#include "core/bus/bus.h"
#include "core/bus/state.h"
#include "hal/roles.h"
#include "hardware/boards/lilygo/t_echo_plus/pins.h"
#include "hardware/parts/l76k/l76k.h"
#include "hardware/parts/ssd1681/ssd1681.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "runtime/null.h"
#include "runtime/tasks.h"
#include "ui/input/button.h"

namespace skyblip::boards {

// The T-Echo Plus, assembled once. P is the platform: silicon or host. Swapping
// it changes which io/ backend the parts talk to and nothing else, so there is
// no second copy of this wiring to keep in step.
template <class P>
class TEchoPlus {
   public:
    TEchoPlus(P& platform, bus::Bus& bus)
        : platform_(platform),
          bus_(bus),
          radio_(platform.spi(io::BusId::Radio), platform.gpio(), t_echo_plus::kRadioBusy,
                 t_echo_plus::kRadioRst, t_echo_plus::kRadioDio1),
          epd_(platform.spi(io::BusId::Epd), platform.gpio(), t_echo_plus::kEpdDc,
               t_echo_plus::kEpdRst, t_echo_plus::kEpdBusy, t_echo_plus::kEpdBacklight),
          gnss_(platform.uart(io::BusId::Gnss)),
          rf_(radio_, platform.clock(), bus.rf),
          capabilities_(platform.capabilities()) {
        platform_.wire(t_echo_plus::kPinMap);
        // The radio is the one part on this board with no bus enumeration of its
        // own, so presence is a register round-trip. Asserting it instead makes a
        // dead MISO look like a healthy radio that never hears anything. It
        // belongs here rather than in begin() because capabilities() has to be
        // final before roles() is taken, and the round-trip needs nothing the
        // rails have not already provided: DS 13.1.1 puts the part in STDBY_RC on
        // reset, and a register write and read back run on that internal clock.
        if (radio_.probe() == Status::Ok) capabilities_ = capabilities_ | hal::Capability::Rf;
    }

    // Probing is done: capabilities() is already known. This is bring-up, and a part that
    // is present but refuses to start is a degraded state, not a missing
    // capability.
    Status begin() {
        const Status s = platform_.begin();
        if (s != Status::Ok) return s;
        if (hal::has(capabilities_, hal::Capability::Display)) epd_.begin();
        if (!hal::has(capabilities_, hal::Capability::Rf)) return Status::Ok;
        return rf_.begin();
    }

    hal::Capabilities capabilities() const { return capabilities_; }

    hal::Roles roles() {
        return hal::Roles{
            platform_.clock(),
            hal::has(capabilities_, hal::Capability::Rf) ? static_cast<hal::Rf&>(rf_) : null_.rf,
            hal::has(capabilities_, hal::Capability::Link)
                ? static_cast<hal::Link&>(platform_.link())
                : null_.link,
            hal::has(capabilities_, hal::Capability::Display) ? static_cast<hal::Display&>(epd_)
                                                              : null_.display,
            hal::has(capabilities_, hal::Capability::Storage)
                ? static_cast<hal::KvStore&>(platform_.kv())
                : null_.kv,
            hal::has(capabilities_, hal::Capability::Storage)
                ? static_cast<hal::FlashRegion&>(platform_.log_flash())
                : null_.log_flash,
            hal::has(capabilities_, hal::Capability::Buzzer)
                ? static_cast<hal::Annunciator&>(platform_.annunciator())
                : null_.annunciator,
            hal::has(capabilities_, hal::Capability::Dfu) ? static_cast<hal::Dfu&>(platform_.dfu())
                                                          : null_.dfu,
            capabilities_,
            platform_.device_addr(),
        };
    }

    // The producer side: everything hardware says arrives on the bus, and the
    // clock's PPS phase is refreshed. Identical on both platforms.
    void poll(bus::State& state, uint32_t now_ms) {
        rf_.service(now_ms);

        // The connection before the bytes: a frame from a session whose Up is
        // still queued behind it would be answered by a service that does not yet
        // believe there is anyone there.
        messages::LinkEvent link_event;
        while (platform_.link().pop_event(link_event)) bus_.link_events.push(link_event);

        messages::RxFrame frame;
        while (platform_.link().pop_rx(frame)) {
            if (frame.endpoint == messages::Endpoint::Log)
                bus_.log_rx.push(frame);
            else
                bus_.link_rx.push(frame);
        }

        if (hal::has(capabilities_, hal::Capability::Gnss)) {
            gnss_.service(now_ms);
            if (gnss_.poll()) bus_.gnss.push(gnss_.fix());
        }

        if (hal::has(capabilities_, hal::Capability::Baro) &&
            now_ms - last_baro_ms_ >= runtime::kBaroPeriodMs) {
            last_baro_ms_ = now_ms;
            uint32_t pa = 0;
            if (platform_.read_pressure_pa(pa)) bus_.baro.push(messages::BaroSample{pa, now_ms});
        }

        if (hal::has(capabilities_, hal::Capability::Battery) &&
            now_ms - last_battery_ms_ >= runtime::kBatteryPeriodMs) {
            last_battery_ms_ = now_ms;
            uint16_t millivolts = 0;
            if (platform_.read_battery_mv(millivolts))
                bus_.battery.push(messages::BatterySample{millivolts, platform_.external_power()});
        }

        if (button_.update(platform_.button_down(), now_ms))
            bus_.input.push(messages::ButtonEvent{0});

        const uint64_t now_us = platform_.clock().micros();
        state.clock.pps_locked = platform_.pps().locked();
        state.clock.ms_since_pps = platform_.pps().ms_since(now_us);
        // The edge itself, as the surface latched it: whoever reads it later
        // reads an instant that has not gone stale in the meantime. Rebuilding
        // it from the millisecond phase threw away up to a millisecond of the
        // 5 ms jitter guard before the plan was even armed.
        state.clock.pps_edge_us = platform_.pps().last_edge_us();
        // The one place the PPS edge is owned: the bench's interval-error
        // histogram is fed here rather than by a second reader of the pin.
        state.timing_stats.record_edge(state.clock.pps_edge_us, state.clock.pps_locked);
    }

    typename P::Rf& rf() { return rf_; }
    parts::L76k& gnss() { return gnss_; }
    parts::Ssd1681& display() { return epd_; }

   private:
    P& platform_;
    bus::Bus& bus_;
    parts::Sx1262 radio_;
    parts::Ssd1681 epd_;
    parts::L76k gnss_;
    typename P::Rf rf_;
    ui::Button button_{};
    runtime::NullRoles null_{};
    hal::Capabilities capabilities_;
    uint32_t last_baro_ms_{0};
    uint32_t last_battery_ms_{0};
};

}  // namespace skyblip::boards

#endif
