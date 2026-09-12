#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_SCREEN_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_SCREEN_H

#include "core/comms/config.h"
#include "runtime/service.h"
#include "ui/framebuffer.h"
#include "ui/input/gesture.h"
#include "ui/screens/boot.h"
#include "ui/screens/confirm.h"
#include "ui/screens/radar.h"
#include "ui/screens/radio_log.h"
#include "ui/screens/settings.h"
#include "ui/screens/signal.h"
#include "ui/screens/sixpack.h"
#include "ui/screens/status.h"

namespace skyblip::go {

enum class Page : uint8_t { Radar, SixPack, Status, Signal, RadioLog, kCount };

enum class Mode : uint8_t { Traffic, Settings };

class ScreenService : public runtime::Service {
   public:
    static constexpr int kMaxRadarTargets = 12;
    static constexpr uint32_t kRenderPeriodMs = 1000;
    static constexpr uint32_t kPresentFloorMs = 1000;
    // INFO: fc 09mar26 Good Display asks for one refresh a day, SoftRF ships none at all
    static constexpr uint32_t kFullEveryMs = 3600000;

    // INFO: fc 06sep26 Good Display rates the glass 0..50 C operating, -20..70 C storage
    static constexpr int16_t kFullOnlyBelowDeciCelsius = 0;
    // INFO: fc 06sep26 the rated limit read on a die above ambient, so it holds early
    static constexpr int16_t kHoldAboveDeciCelsius = 500;

    // INFO: cf 02aug26 The level at which the radar carries a bearing worth
    // turning the head for. At or above it the settings page gives the glass
    // back on its own: a menu in front of converging traffic is a bug.
    static constexpr uint8_t kAlarmTakesGlass = 2;

    using runtime::Service::Service;

    void tick(uint32_t now_ms) override;

    // The one consumer of bus.input, and therefore the one place a press is
    // given a meaning. The companion link's state machine is handed over here
    // so that meaning can be "authorise this" when, and only when, a prompt the
    // pilot can read is on the glass.
    void attach_config(comms::ConfigService& config) { config_ = &config; }

    void attach_self_test(const ui::BootSnapshot& snapshot) { self_test_ = &snapshot; }

    void next_page();
    void set_backlight(bool on);
    void set_power(bool on);
    void settle_park(uint32_t now_ms);
    void park_for_install();
    void park_for_stow();
    void set_range_m(int32_t m) {
        range_m_ = m;
        dirty_ = true;
    }

    enum class Thermal : uint8_t { Refresh, FullOnly, Hold };
    Thermal thermal() const;

    Page page() const { return page_; }
    Mode mode() const { return mode_; }
    comms::Pending prompt() const { return prompt_; }
    const ui::SettingsEditor& editor() const { return editor_; }
    bool showing_self_test() const { return showing_self_test_; }
    int32_t range_m() const { return range_m_; }
    bool backlight() const { return backlight_; }
    bool powered() const { return powered_; }
    bool parking() const { return park_ != ParkStep::None; }
    const ui::Framebuffer& framebuffer() const { return fb_; }
    void mark_dirty() { dirty_ = true; }
    int fasts_since_full() const { return fasts_since_full_; }

   private:
    void render();
    void repaint_through_black();
    void draw_prompt();
    void draw_settings_page();
    void dismiss_self_test(uint32_t now_ms);
    void enter_settings(uint32_t now_ms);
    void leave_settings();
    void handle_input(uint32_t now_ms);
    void sync_editor(uint32_t now_ms);
    void step_editor(uint32_t now_ms);
    void resolve(ui::Gesture gesture);
    Page traffic_page() const;
    bool transitions_through_black() const;
    void present_black_flash(uint32_t now_ms);
    bool decide_full(uint32_t now_ms) const;
    bool may_present_park_frame() const;
    enum class ParkFrame : uint8_t { Wordmark, Installing, Blank };
    enum class ParkStep : uint8_t { None, Frame, Sleep };
    void park(ParkFrame frame);
    void draw_park_frame(ParkFrame frame);
    void note_presented(hal::Refresh mode, uint32_t now_ms);

    // 1 m/s = 196.85 ft/min, from eighth-m/s.
    int32_t climb_fpm() const {
        return (static_cast<int32_t>(context_.state.own.climb_e8) * 19685) / (8 * 100);
    }

    comms::ConfigService* config_{nullptr};
    const ui::BootSnapshot* self_test_{nullptr};
    comms::Pending prompt_{comms::Pending::None};
    ui::ConfirmGesture gesture_{};
    ui::SettingsEditor editor_{};

    // INFO: cf 02aug26 What arms the authorising gesture: the prompt has
    // reached the glass, and the thumb has been still for a whole double-press
    // window. A pilot stepping a value on the settings page taps faster than
    // that, so a prompt landing mid-stream cannot be answered by the presses
    // already on their way - it has to be read first, and then answered.
    uint32_t last_press_ms_{0};
    uint32_t prompt_since_ms_{0};
    bool pressed_once_{false};
    bool prompt_on_glass_{false};

    ui::Framebuffer fb_{};
    ui::Framebuffer presented_{};
    ui::RadarTarget targets_[kMaxRadarTargets]{};
    traffic::LinkRow signal_rows_[ui::kSignalRows]{};
    Page page_{Page::Radar};
    Mode mode_{Mode::Traffic};
    int32_t range_m_{10000};
    uint32_t last_tick_ms_{0};
    uint32_t last_render_ms_{0};
    uint32_t last_present_ms_{0};
    uint32_t last_full_ms_{0};
    int fasts_since_full_{0};
    uint8_t last_alarm_{0};
    bool dirty_{true};
    bool want_full_{true};
    bool flash_pending_{false};
    bool flashed_{false};
    bool presented_once_{false};
    bool showing_self_test_{false};
    ParkStep park_{ParkStep::None};
    ParkFrame park_frame_{ParkFrame::Wordmark};
    bool backlight_{false};
    bool powered_{true};
};

}  // namespace skyblip::go

#endif
