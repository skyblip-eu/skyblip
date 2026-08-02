#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PRODUCT_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PRODUCT_H

#include "core/power/reset_reason.h"
#include "core/power/shutdown.h"
#include "hardware/boards/lilygo/t_echo_plus/board.h"
#include "products/skyblip_go/services/alarm.h"
#include "products/skyblip_go/services/config.h"
#include "products/skyblip_go/services/ownship.h"
#include "products/skyblip_go/services/power.h"
#include "products/skyblip_go/services/radio.h"
#include "products/skyblip_go/services/screen.h"
#include "products/skyblip_go/services/traffic.h"
#include "runtime/loop.h"
#include "ui/screens/boot.h"

namespace skyblip::go {

enum class Feature : uint32_t {
    AdslRx = 1u << 0,
    UplinkRx = 1u << 1,
    AdslTx = 1u << 6,
    // Receive only: the same M-band dwell as AdslRx, framed by the sync window
    // the two systems share (core/protocol/air.h). We never transmit it.
    AlptasRx = 1u << 7,
    Radar = 1u << 2,
    Alarms = 1u << 3,
    Instruments = 1u << 4,
    CompanionLink = 1u << 5,
};

constexpr Feature kFeatures = static_cast<Feature>(
    static_cast<uint32_t>(Feature::AdslRx) | static_cast<uint32_t>(Feature::UplinkRx) |
    static_cast<uint32_t>(Feature::Radar) | static_cast<uint32_t>(Feature::Alarms) |
    static_cast<uint32_t>(Feature::Instruments) | static_cast<uint32_t>(Feature::CompanionLink) |
    static_cast<uint32_t>(Feature::AdslTx) | static_cast<uint32_t>(Feature::AlptasRx));

// What this product cannot fly without, and what it can lose and keep flying.
constexpr hal::Capabilities kRequired = hal::Capability::Rf | hal::Capability::Gnss;
constexpr hal::Capabilities kOptional =
    hal::Capability::Display | hal::Capability::Baro | hal::Capability::Buzzer |
    hal::Capability::Vibro | hal::Capability::Link | hal::Capability::Storage |
    hal::Capability::Dfu | hal::Capability::Button | hal::Capability::Battery;

struct BootPartSpec {
    const char* name;
    hal::Capability capability;
};

// The inventory the self-test page reads out, in the order a bench eye wants
// it: what the device cannot fly without first.
constexpr BootPartSpec kBootParts[] = {
    {"RADIO", hal::Capability::Rf},      {"GNSS", hal::Capability::Gnss},
    {"PANEL", hal::Capability::Display}, {"BARO", hal::Capability::Baro},
    {"BUTTON", hal::Capability::Button}, {"BATTERY", hal::Capability::Battery},
    {"LINK", hal::Capability::Link},     {"STORAGE", hal::Capability::Storage},
    {"DFU", hal::Capability::Dfu},       {"BUZZER", hal::Capability::Buzzer},
    {"VIBRO", hal::Capability::Vibro},
};

constexpr int kBootPartCount = static_cast<int>(sizeof(kBootParts) / sizeof(kBootParts[0]));
static_assert(kBootPartCount <= ui::kBootRows, "the self-test page would drop a part");

// skyBlip Go: one board, one service list. The shell around it only decides how
// often step() is called and where the pixels go.
template <class P>
class Product {
   public:
    using Board = boards::TEchoPlus<P>;

    explicit Product(P& platform) : platform_(platform), board_(platform, bus_) {}

    Status setup() {
        const Status board = board_.begin();
        reset_reason_ = power::classify(platform_.system_power().reset_causes());
        // The register is read once and then only remembered, so the companion
        // link is told at boot: a watchdog bite in the field is diagnosable
        // from a phone, without the panel in hand.
        config_.config().set_reset_reason(reset_reason_);
        flyable_ = board == Status::Ok &&
                   hal::missing(board_.capabilities(), kRequired) == hal::Capability::None;

        // The panel is painted before anything is allowed to refuse: a device
        // that names the part that failed is worth more on a first flash than a
        // device that returns from main and goes dark.
        show_boot_page();

        if (board != Status::Ok) return board;
        if (!flyable_) return Status::Down;
        const Status loop = loop_.setup();
        state_.started = loop == Status::Ok;
        return loop;
    }

    void step(uint32_t now_ms) {
        if (flyable_ && !shutdown_.going_down()) {
            board_.poll(state_, now_ms);
            loop_.step(now_ms);
            if (power_.cutoff()) shutdown_.request(power::ShutdownReason::LowBattery, now_ms);
            if (config_.config().power_off_requested()) {
                config_.config().clear_power_off_request();
                shutdown_.request(power::ShutdownReason::LinkRequest, now_ms);
            }
        }
        shutdown_.tick(now_ms, platform_.button_down());
        drive_shutdown();
    }

    hal::Capabilities capabilities() const { return board_.capabilities(); }
    hal::Capabilities degraded() const { return hal::missing(board_.capabilities(), kOptional); }

    // False when a required capability is missing: the loop refuses to fly, the
    // self-test page stays on the glass and the button still works.
    bool flyable() const { return flyable_; }
    power::ResetReason reset_reason() const { return reset_reason_; }
    const ui::Framebuffer& boot_page() const { return boot_fb_; }

    power::ShutdownSequencer& shutdown() { return shutdown_; }
    const power::ShutdownSequencer& shutdown() const { return shutdown_; }
    bool ready_to_power_off() const { return shutdown_.ready_to_power_off(); }

    // Feeding through a deliberate shutdown is correct: the device is doing what
    // it was told, and a held button must not turn a power-off into a reboot.
    bool may_feed_watchdog(uint32_t now_ms) const {
        if (!flyable_ || shutdown_.going_down()) return true;
        return loop_.may_feed_watchdog(now_ms);
    }
    int stalled_service(uint32_t now_ms) const { return loop_.stalled_service(now_ms); }
    const char* service_name(int index) const { return loop_.feed_decision().name(index); }

    Board& board() { return board_; }
    bus::Bus& bus() { return bus_; }
    bus::State& state() { return state_; }
    const bus::State& state() const { return state_; }

    OwnshipService& ownship() { return ownship_; }
    PowerService& power() { return power_; }
    RadioService& radio() { return radio_; }
    TrafficService& traffic() { return traffic_; }
    AlarmService& alarm() { return alarm_; }
    ScreenService& screen() { return screen_; }
    ConfigLinkService& config() { return config_; }

   private:
    void show_boot_page() {
        const hal::Capabilities fitted = board_.capabilities();
        for (int i = 0; i < kBootPartCount; i++) {
            const BootPartSpec& spec = kBootParts[i];
            boot_parts_[i].name = spec.name;
            boot_parts_[i].state = hal::has(fitted, spec.capability)      ? ui::PartState::Pass
                                   : hal::has(kRequired, spec.capability) ? ui::PartState::Fail
                                                                          : ui::PartState::Absent;
        }

        ui::BootSnapshot snapshot;
        snapshot.device_addr = roles_.device_addr;
        snapshot.reset_reason = power::to_string(reset_reason_);
        snapshot.parts = boot_parts_;
        snapshot.n_parts = kBootPartCount;
        snapshot.flyable = flyable_;
        snapshot.battery_valid = state_.battery.valid;
        snapshot.battery_mv = state_.battery.millivolts;
        ui::draw_boot(boot_fb_, snapshot);
        roles_.display.present(boot_fb_, hal::Refresh::Full, 0);
    }

    void drive_shutdown() {
        const power::ShutdownPhase phase = shutdown_.phase();
        if (phase == acted_phase_) return;
        acted_phase_ = phase;
        if (phase != power::ShutdownPhase::Parking) return;
        // The radio goes first. An armed dwell keeps the receiver and the PA
        // alive right through the seconds the panel takes to park.
        roles_.rf.abort();
        roles_.rf.sleep();
        screen_.set_power(false);
    }

    bus::Bus bus_{};
    bus::State state_{};
    P& platform_;
    Board board_;
    hal::Roles roles_{board_.roles()};
    runtime::Context ctx_{roles_, bus_, state_};

    ConfigLinkService config_{ctx_};
    OwnshipService ownship_{ctx_};
    PowerService power_{ctx_};
    RadioService radio_{ctx_};
    TrafficService traffic_{ctx_};
    AlarmService alarm_{ctx_};
    ScreenService screen_{ctx_};

    static constexpr int kServiceCount = 7;
    runtime::Service* services_[kServiceCount]{&config_,  &ownship_, &power_, &radio_,
                                               &traffic_, &alarm_,   &screen_};
    static constexpr const char* kServiceNames[kServiceCount] = {
        "config", "ownship", "power", "radio", "traffic", "alarm", "screen"};
    runtime::Loop loop_{services_, kServiceCount, kServiceNames};

    ui::Framebuffer boot_fb_{};
    ui::BootPart boot_parts_[kBootPartCount]{};
    power::ShutdownSequencer shutdown_{};
    power::ShutdownPhase acted_phase_{power::ShutdownPhase::Running};
    power::ResetReason reset_reason_{power::ResetReason::Unknown};
    bool flyable_{false};
};

}  // namespace skyblip::go

#endif
