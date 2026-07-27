// products/skyblip_go/app.h — THE PRODUCT: all of skyBlip Go's wiring and
// service logic in one pure, host-testable object, MINUS the framework.
//
// 3-ARCHITECTURE §8 acceptance invariant: "products/skyblip_go links natively
// with zero source changes (swap only the board)." App touches NO framework
// header — it depends only on core/, hal/ ports and the shared drivers — which
// is what makes the two shells beside it possible:
//
//   device/     the board on silicon, driven by a Zephyr main()
//   simulator/  the same board out of devices/models, driven by two frontends
//
// A shell does exactly two things: construct the adapters for its world, and
// drive App::setup()/App::step(). Anything else belongs in here.
#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_APP_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_APP_H

#include "core/comms/config.h"
#include "core/flight/atmosphere.h"
#include "core/gnss/nmea.h"
#include "core/messages/messages.h"
#include "core/settings/settings.h"
#include "core/timing/slot.h"
#include "core/traffic/table.h"
#include "devices/drivers/sx1262.h"
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

namespace skyblip::go {

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

    // Deliver a new GNSS fix. The shell polls drivers::L76k and pushes; App
    // applies it inside the next step(), where it has `now_ms` to derive
    // vertical speed against. Producers enqueue, App consumes on its own clock.
    void on_gnss_fix(const gnss::GnssFix& fix) {
        pending_fix_ = fix;
        have_pending_fix_ = true;
    }

    // Deliver a barometric pressure sample. Vertical speed comes from pressure
    // whenever one of these has arrived, because a RATE needs no agreement with
    // anyone about datum, and differentiated GNSS altitude is our noisiest
    // signal. The altitude we BROADCAST stays GNSS: the alarm compares relative
    // altitude between aircraft, so the datum is a protocol question.
    void on_baro(uint32_t pressure_pa, uint32_t now_ms);

    bool baro_active() const { return baro_ref_ms_ != 0; }

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

    // Vertical speed from the altitude trend over a window, shared by the GNSS
    // and barometric paths. Returns false until the window has elapsed.
    bool vs_from_alt_cm(int32_t alt_cm, uint32_t now_ms, uint32_t window_ms, int32_t& ref_alt_cm,
                        uint32_t& ref_ms, int16_t& out_e8) const;

    Ports p_;
    settings::Settings settings_{};
    timing::Scheduler scheduler_{};
    traffic::TrafficTable table_{};
    comms::ConfigService config_;

    gnss::GnssFix pending_fix_{};
    messages::OwnState own_{};
    timing::ClockState clock_state_{};
    timing::SlotPlan plan_{};

    uint32_t last_ms_{0};
    uint32_t last_render_ms_{0};
    uint32_t gnss_fixes_{0};
    int32_t vs_ref_alt_cm_{0};
    uint32_t vs_ref_ms_{0};
    int32_t baro_ref_alt_cm_{0};
    uint32_t baro_ref_ms_{0};
    uint32_t rx_ok_{0};
    uint32_t rx_bad_{0};
    uint8_t max_alarm_{0};
    int32_t range_m_{10000};
    bool started_{false};
    bool image_confirmed_{false};
    bool dirty_{true};
    bool backlight_{false};
    bool have_pending_fix_{false};
    Page page_{Page::Radar};
    uint8_t rx_buf_[64]{};
    ui::Framebuffer fb_{};
    ui::RadarTarget targets_[kMaxRadarTargets]{};

    // core/traffic/alarm.h grades contacts 1 info, 2 important, 3 urgent.
    static constexpr uint8_t kVibroFromLevel = 2;
    static constexpr uint8_t kUrgentLevel = 3;
    // Long enough to feel through a glove and a harness strap, short enough not
    // to blur into the next escalation.
    static constexpr uint16_t kVibroImportantMs = 200;
    static constexpr uint16_t kVibroUrgentMs = 600;

    static constexpr uint32_t kRenderPeriodMs = 1000;
    static constexpr uint32_t kVsWindowMs = 2000;
    // Pressure is far quieter than differentiated GNSS altitude, so the same
    // confidence needs a shorter window - which is the point of having a baro.
    static constexpr uint32_t kBaroVsWindowMs = 1000;
};

}  // namespace skyblip::go

#endif
