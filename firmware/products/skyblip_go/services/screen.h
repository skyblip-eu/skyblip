#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_SCREEN_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_SCREEN_H

#include "runtime/service.h"
#include "ui/framebuffer.h"
#include "ui/screens/radar.h"
#include "ui/screens/signal.h"
#include "ui/screens/sixpack.h"
#include "ui/screens/status.h"

namespace skyblip::go {

enum class Page : uint8_t { Radar, SixPack, Status, Signal, kCount };

// Draws the current page once a second and presents it only when the pixels
// changed. Fast (differential, flicker-free) refresh is the default; the full
// flashing refresh that clears e-paper ghosting is scheduled around the traffic
// picture: never during an alarm, opportunistically when the sky has been empty
// for a while, forced only past a hard ceiling.
class ScreenService : public runtime::Service {
   public:
    static constexpr int kMaxRadarTargets = 12;
    static constexpr uint32_t kRenderPeriodMs = 1000;  // draw + diff cadence
    static constexpr uint32_t kPresentFloorMs = 1000;  // min gap between refreshes
    static constexpr int kFastPerFull = 12;      // fast refreshes before a full is owed
    static constexpr int kFastHardCeiling = 24;  // owed full forced, alarm or not
    static constexpr uint32_t kSkyEmptyBeforeFullMs = 60000;  // quiet time before an
                                                              // owed full is taken early
    static constexpr uint32_t kFullEveryMs = 0;  // wall-clock full floor, 0 = disabled

    using runtime::Service::Service;

    void tick(uint32_t now_ms) override;

    void next_page();
    void set_backlight(bool on);
    void set_power(bool on);
    void set_range_m(int32_t m) {
        range_m_ = m;
        dirty_ = true;
    }

    Page page() const { return page_; }
    int32_t range_m() const { return range_m_; }
    bool backlight() const { return backlight_; }
    bool powered() const { return powered_; }
    const ui::Framebuffer& framebuffer() const { return fb_; }
    void mark_dirty() { dirty_ = true; }
    int fasts_since_full() const { return fasts_since_full_; }

   private:
    void render();
    bool decide_full(uint32_t now_ms, bool quiet) const;
    void note_presented(hal::Refresh mode, uint32_t now_ms);

    // 1 m/s = 196.85 ft/min, from eighth-m/s.
    int32_t climb_fpm() const {
        return (static_cast<int32_t>(context_.state.own.climb_e8) * 19685) / (8 * 100);
    }

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
    bool want_full_{true};  // first present, page changes, repower: prefer full
    bool presented_once_{false};
    bool backlight_{false};
    bool powered_{true};
};

}  // namespace skyblip::go

#endif
