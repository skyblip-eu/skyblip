#ifndef SKYBLIP_HARDWARE_BOARDS_T_ECHO_PLUS_BOARD_H
#define SKYBLIP_HARDWARE_BOARDS_T_ECHO_PLUS_BOARD_H

#include "core/bus/bus.h"
#include "core/bus/state.h"
#include "hal/inventory.h"
#include "hal/roles.h"
#include "hardware/boards/lilygo/t_echo_plus/i2c_scan.h"
#include "hardware/boards/lilygo/t_echo_plus/pins.h"
#include "hardware/parts/drv2605/drv2605.h"
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
          gnss_(platform.uart(io::BusId::Gnss), platform.uart_rate(io::BusId::Gnss)),
          haptic_(platform.i2c(io::BusId::Sensor), platform.gpio(), t_echo_plus::kVibro),
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

        take_bus_inventory();
        identify_panel();
        establish_haptic();
        if (platform_.buzzer_pin_held_low()) capabilities_ = without(hal::Capability::Buzzer);
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

    // The way down, for the parts the board owns rather than the platform: the
    // haptic driver goes to standby and lets go of its enable line before the
    // rail it hangs off collapses. SoftRF does the same two things in the same
    // order (platform/nRF52.cpp:2899-2912). Called from the shutdown sequence,
    // after the services have stopped running.
    void park() { haptic_.park(); }

    hal::Capabilities capabilities() const { return capabilities_; }

    // What answered, by name and address, behind the capability bits. The
    // self-test page reads it: two BME280 addresses, five shipped e-paper
    // signatures and a haptic that may or may not be a waveform driver are
    // exactly the facts a bench cannot get from a PASS.
    const hal::Inventory& inventory() const { return inventory_; }

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
            // Buzzer OR haptic: they are one role and two parts, and a board with
            // a dead buzzer pin still owes a pilot the pulse it can make.
            hal::has(capabilities_, hal::Capability::Buzzer) ||
                    hal::has(capabilities_, hal::Capability::Vibro)
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
        // A haptic pulse ends on a deadline the adapter owns. On silicon that is a
        // work item the kernel runs; on the host it is this call, so a motor left
        // running is a bug a host test can catch.
        platform_.annunciator().service(now_ms);

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
    parts::Drv2605& haptic() { return haptic_; }

   private:
    // hal::missing(a, b) is "b without a", which is what this board needs when a
    // probe contradicts what the platform declared.
    hal::Capabilities without(hal::Capability bit) const {
        return hal::missing(bit, capabilities_);
    }

    // The fingerprint is taken where the pins are free, which on silicon is the
    // board port at PRE_KERNEL_1 and on the host is the panel model. Either way
    // the driver is told once, before begin(), and an unread fingerprint leaves
    // the panel Unknown - which is the safest refresh policy and, until somebody
    // reads a Plus on a bench, the honest answer for this board.
    void identify_panel() {
        parts::PanelSignature signature{};
        platform_.read_panel_signature(signature);
        epd_.adopt(signature);
        inventory_.panel = epd_.panel_name();
    }

    void take_bus_inventory() {
        // The enable line first: it costs one register write, and if it turns out
        // to gate the driver's supply rather than its standby, a scan taken before
        // it would miss the part entirely.
        haptic_.power_up();
        inventory_ = t_echo_plus::scan_i2c(platform_.i2c(io::BusId::Sensor));
        inventory_.baro_address = inventory_.has_i2c_address(t_echo_plus::kBaroAddrPrimary)
                                      ? t_echo_plus::kBaroAddrPrimary
                                  : inventory_.has_i2c_address(t_echo_plus::kBaroAddrAlternate)
                                      ? t_echo_plus::kBaroAddrAlternate
                                      : 0;
    }

    // The haptic is a part on a bus, not a pin. Capability::Vibro is granted by
    // the part answering and identifying itself, never by the board being a Plus:
    // P0.08 high with no DRV2605 behind it is a device that claims a vibration
    // motor and cannot vibrate, which is what the alarm's escalation to haptics
    // was until this probe existed.
    //
    // INFO: fc 06aug26 NOBODY HAS SEEN 0x5A ANSWER ON OUR OWN HARDWARE. The
    // address, the identification and the whole pulse sequence come from SoftRF,
    // which identifies the T-Echo Plus by exactly this probe
    // (src/platform/nRF52.cpp:1158-1163, driver at 2112-2133). If a bench unit
    // does not answer, this reports the haptic absent and the alarm loses it
    // honestly, which is still better than the claim that preceded it.
    void establish_haptic() {
        capabilities_ = without(hal::Capability::Vibro);
        inventory_.haptic = hal::HapticKind::None;
        if (!inventory_.has_i2c_address(t_echo_plus::kHapticDriverAddress)) return;
        if (haptic_.begin() != Status::Ok) return;
        platform_.annunciator().attach_haptic(haptic_);
        inventory_.haptic = hal::HapticKind::WaveformDriver;
        capabilities_ = capabilities_ | hal::Capability::Vibro | hal::Capability::HapticDriver;
    }

    P& platform_;
    bus::Bus& bus_;
    parts::Sx1262 radio_;
    parts::Ssd1681 epd_;
    parts::L76k gnss_;
    parts::Drv2605 haptic_;
    typename P::Rf rf_;
    hal::Inventory inventory_{};
    ui::Button button_{};
    runtime::NullRoles null_{};
    hal::Capabilities capabilities_;
    uint32_t last_baro_ms_{0};
    uint32_t last_battery_ms_{0};
};

}  // namespace skyblip::boards

#endif
