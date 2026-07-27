// products/skyblip/app.h — the composition root, MINUS the framework.
//
// 3-ARCHITECTURE §8 acceptance invariant: "products/skyblip links for env:native
// with zero source changes (swap only the board)." `App` holds ALL the wiring
// and service logic in one pure, host-testable object, OUT of the framework
// entry point.
//
// The framework shell (Zephyr main() — see products/skyblip/main.cpp) does
// exactly two things: (1) construct the concrete adapters for its SoC, and
// (2) drive App::setup()/App::step(). App itself touches NO framework header
// (no Zephyr) — it depends only on core/, hal/ ports and the shared drivers.
// That is what keeps it linkable and testable on the host.
#ifndef SKYBLIP_PRODUCTS_SKYBLIP_APP_H
#define SKYBLIP_PRODUCTS_SKYBLIP_APP_H

#include "core/comms/config.h"
#include "core/gnss/nmea.h"
#include "core/messages/messages.h"
#include "core/settings/settings.h"
#include "core/timing/slot.h"
#include "core/traffic/table.h"
#include "devices/drivers/sx1262.h"
#include "devices/io/io.h"
#include "hal/annunciator.h"
#include "hal/clock.h"
#include "hal/dfu.h"
#include "hal/display.h"
#include "hal/kvstore.h"
#include "hal/link.h"
#include "ui/framebuffer.h"
#include "ui/screens/altvs.h"
#include "ui/screens/radar.h"
#include "ui/screens/status.h"

namespace skyblip::product {

// The e-paper pages (roadmap 2.6d). settings.page_mask (0x07) enables/disables
// them individually; the button cycles through the enabled ones.
enum class Page : uint8_t { Radar, AltVs, Status, kCount };

// The port surface. The composition root fills this in with concrete adapters.
// Required ports are references (always present); optional capabilities are
// nullable pointers (a headless / non-Plus board simply passes nullptr).
struct Ports {
    hal::Clock& clock;
    hal::Link& link;
    drivers::Sx1262& radio;

    hal::Display* display{nullptr};  // EPD (skyBlip Go); nullptr => headless
    hal::KvStore* kv{nullptr};       // durable settings; nullptr => defaults
    hal::Annunciator* annunciator{nullptr};
    hal::Dfu* dfu{nullptr};
    io::Uart* gnss{nullptr};  // GNSS serial (L76K)

    uint32_t device_addr{0};  // SoC unique id → default ADS-L address
};

// One object, many shells (device / simulator / tests).
class App {
   public:
    static constexpr int kMaxRadarTargets = 12;

    explicit App(const Ports& ports);

    // Bring the device up: load settings, start the radio in Rx. Idempotent.
    Status setup();

    // One cooperative iteration. `now_ms` is hal::Clock::millis() — passing it
    // in (rather than reading the clock inside) keeps App deterministic under a
    // modelled clock in host tests.
    void step(uint32_t now_ms);

    // Deliver a companion-link RX frame to the config state machine. The shell's
    // BLE adapter enqueues frames; the shell drains them into here.
    void on_link_rx(const messages::RxFrame& frame) { config_.on_rx(frame); }

    // UI input from the shell (button press). Cycles the page and forces a full
    // e-paper refresh on the next step.
    void on_button();
    void set_backlight(bool on);
    Page page() const { return page_; }
    bool backlight() const { return backlight_; }

    // Radar range (metres) — cycled by the UI / set by tests and the simulator.
    void set_range_m(int32_t m) {
        range_m_ = m;
        dirty_ = true;
    }
    int32_t range_m() const { return range_m_; }

    // Narrow test / introspection accessors.
    settings::Settings& settings() { return settings_; }
    traffic::TrafficTable& traffic() { return table_; }
    comms::ConfigService& config() { return config_; }
    const messages::OwnState& own() const { return own_; }
    int traffic_count() const { return table_.count(); }
    uint8_t max_alarm() const { return max_alarm_; }
    uint32_t rx_ok() const { return rx_ok_; }
    uint32_t rx_bad() const { return rx_bad_; }
    const timing::SlotPlan& last_plan() const { return plan_; }
    bool started() const { return started_; }

   private:
    void load_settings();
    void confirm_image_once_healthy();
    void drain_gnss();
    void apply_gnss(uint32_t now_ms);
    void drain_radio(uint32_t now_ms);
    void update_alarms();
    void render();

    // The traffic table's single time base, in seconds. Observations are stamped
    // with it (rx_utc) and aged against it, so the two MUST agree: mixing GNSS
    // epoch with boot-relative seconds underflows uint32 and ages every target
    // out instantly. Prefer real UTC; fall back to uptime before first fix.
    uint32_t traffic_now(uint32_t now_ms) const {
        return own_.utc_valid ? own_.utc : now_ms / 1000;
    }

    Ports p_;
    settings::Settings settings_{};
    timing::Scheduler scheduler_{};
    traffic::TrafficTable table_{};
    gnss::NmeaParser gnss_{};
    comms::ConfigService config_;

    messages::OwnState own_{};
    timing::ClockState clock_state_{};
    timing::SlotPlan plan_{};

    uint32_t last_ms_{0};
    uint32_t last_render_ms_{0};
    uint32_t gnss_updates_{0};
    int32_t vs_ref_alt_m_{0};
    uint32_t vs_ref_ms_{0};
    uint32_t rx_ok_{0};
    uint32_t rx_bad_{0};
    uint8_t max_alarm_{0};
    int32_t range_m_{10000};
    bool started_{false};
    bool image_confirmed_{false};
    bool dirty_{true};
    bool backlight_{false};
    Page page_{Page::Radar};
    uint8_t rx_buf_[64]{};
    ui::Framebuffer fb_{};
    ui::RadarTarget targets_[kMaxRadarTargets]{};

    static constexpr uint32_t kRenderPeriodMs = 1000;
    static constexpr uint32_t kVsWindowMs = 2000;
};

}  // namespace skyblip::product

#endif
