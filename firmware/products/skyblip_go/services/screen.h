#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_SCREEN_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_SCREEN_H

#include "core/comms/config.h"
#include "runtime/service.h"
#include "ui/framebuffer.h"
#include "ui/input/gesture.h"
#include "ui/screens/confirm.h"
#include "ui/screens/radar.h"
#include "ui/screens/settings.h"
#include "ui/screens/signal.h"
#include "ui/screens/sixpack.h"
#include "ui/screens/status.h"

namespace skyblip::go {

// Settings is last and, unlike the four before it, is not in settings.page_mask:
// it is where the mask is changed, so a mask that hid it would be a mask nobody
// could undo without a phone.
enum class Page : uint8_t { Radar, SixPack, Status, Signal, Settings, kCount };

class ScreenService : public runtime::Service {
   public:
    static constexpr int kMaxRadarTargets = 12;
    static constexpr uint32_t kRenderPeriodMs = 1000;
    static constexpr uint32_t kPresentFloorMs = 1000;
    static constexpr int kFastPerFull = 12;
    static constexpr int kFastHardCeiling = 24;
    static constexpr uint32_t kSkyEmptyBeforeFullMs = 60000;
    static constexpr uint32_t kFullEveryMs = 0;  // 0 disables

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

    void next_page();
    void set_backlight(bool on);
    void set_power(bool on);
    void set_range_m(int32_t m) {
        range_m_ = m;
        dirty_ = true;
    }

    Page page() const { return page_; }
    comms::Pending prompt() const { return prompt_; }
    const ui::SettingsEditor& editor() const { return editor_; }
    int32_t range_m() const { return range_m_; }
    bool backlight() const { return backlight_; }
    bool powered() const { return powered_; }
    const ui::Framebuffer& framebuffer() const { return fb_; }
    void mark_dirty() { dirty_ = true; }
    int fasts_since_full() const { return fasts_since_full_; }

   private:
    void render();
    void draw_prompt();
    void draw_settings_page();
    void handle_input(uint32_t now_ms);
    void sync_editor(uint32_t now_ms);
    void step_editor(uint32_t now_ms);
    void resolve(ui::Gesture gesture);
    Page traffic_page() const;
    void annunciate_first_fix(uint32_t now_ms);
    bool decide_full(uint32_t now_ms, bool quiet) const;
    void note_presented(hal::Refresh mode, uint32_t now_ms);

    // 1 m/s = 196.85 ft/min, from eighth-m/s.
    int32_t climb_fpm() const {
        return (static_cast<int32_t>(context_.state.own.climb_e8) * 19685) / (8 * 100);
    }

    // A fix is worth one chirp, not an alarm: the lowest tone step, briefly, and
    // no haptics at all - the motor is reserved for traffic that escalated, so a
    // pilot who feels it knows what it means without looking.
    static constexpr uint32_t kFirstFixToneMs = 250;
    static constexpr uint8_t kFirstFixToneLevel = 1;

    uint32_t tone_since_ms_{0};
    bool tone_on_{false};

    comms::ConfigService* config_{nullptr};
    comms::Pending prompt_{comms::Pending::None};
    ui::ConfirmGesture gesture_{};
    ui::SettingsEditor editor_{};

    // INFO: cf 02aug26 What arms the authorising gesture: the prompt has
    // reached the glass, and the thumb has been still for a whole double-press
    // window. A pilot stepping a value on the settings page taps faster than
    // that, so a prompt landing mid-stream cannot be answered by the presses
    // already on their way - it has to be read first, and then answered.
    uint32_t last_press_ms_{0};
    bool pressed_once_{false};
    bool prompt_on_glass_{false};

    ui::Framebuffer fb_{};
    ui::Framebuffer presented_{};
    ui::RadarTarget targets_[kMaxRadarTargets]{};
    traffic::LinkRow signal_rows_[ui::kSignalRows]{};
    Page page_{Page::Radar};
    int32_t range_m_{10000};
    uint32_t last_render_ms_{0};
    uint32_t last_present_ms_{0};
    uint32_t last_full_ms_{0};
    uint32_t quiet_since_ms_{0};
    int fasts_since_full_{0};
    uint8_t last_alarm_{0};
    bool dirty_{true};
    bool want_full_{true};
    bool presented_once_{false};
    bool backlight_{false};
    bool powered_{true};
};

}  // namespace skyblip::go

#endif
