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

class ScreenService : public runtime::Service {
   public:
    static constexpr int kMaxRadarTargets = 12;
    static constexpr uint32_t kRenderPeriodMs = 1000;

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
    const ui::Framebuffer& framebuffer() const { return fb_; }
    void mark_dirty() { dirty_ = true; }

   private:
    void render();

    // 1 m/s = 196.85 ft/min, from eighth-m/s.
    int32_t climb_fpm() const {
        return (static_cast<int32_t>(context_.state.own.climb_e8) * 19685) / (8 * 100);
    }

    ui::Framebuffer fb_{};
    ui::RadarTarget targets_[kMaxRadarTargets]{};
    traffic::LinkRow signal_rows_[ui::kSignalRows]{};
    Page page_{Page::Radar};
    int32_t range_m_{10000};
    uint32_t last_render_ms_{0};
    uint8_t last_alarm_{0};
    bool dirty_{true};
    bool backlight_{false};
    bool powered_{true};
};

}  // namespace skyblip::go

#endif
