#ifndef SKYBLIP_HARDWARE_PARTS_SSD1681_H
#define SKYBLIP_HARDWARE_PARTS_SSD1681_H

#include "hal/display.h"
#include "hardware/io/io.h"
#include "ui/framebuffer.h"

namespace skyblip::parts {

class Ssd1681 : public hal::Display {
   public:
    Ssd1681(io::Spi& spi, io::Gpio& gpio, int dc, int rst, int busy, int backlight = -1)
        : spi_(spi), gpio_(gpio), dc_(dc), rst_(rst), busy_(busy), backlight_(backlight) {}

    void begin();
    void present(const ui::Framebuffer& fb, hal::Refresh mode, uint32_t now_ms) override;
    bool ready(uint32_t now_ms) override;
    void power_off() override;
    void power_on() override { begin(); }
    void set_backlight(bool on) override;

    // INFO: fc 01aug25 GDEH0154D67 settles in ~460 ms fast / ~2.5 s full (GxEPD2-measured)
    static constexpr uint32_t kReadyAfterFastMs = 300;
    static constexpr uint32_t kReadyAfterFullMs = 1500;
    static constexpr uint32_t kBusyTimeoutMs = 5000;

   private:
    void init_panel();
    void finish_refresh();
    void enter_sleep();
    void cmd(uint8_t c);
    void data(uint8_t d);
    void write_bank(uint8_t command, const uint8_t* fb_bytes);
    void set_window(int x0, int y0, int x1, int y1);
    void set_cursor(int x, int y);
    void wait_busy(uint32_t max_spins = 200000);

    io::Spi& spi_;
    io::Gpio& gpio_;
    int dc_, rst_, busy_, backlight_;
    // INFO: fc 01aug25 the glass image; rewritten into bank 0x26 each present so
    // fast refreshes diff against the truth across deep sleep and panel lots
    uint8_t shadow_[ui::Framebuffer::kBytes]{};
    bool glass_known_{false};
    bool asleep_{false};
    bool refreshing_{false};
    uint32_t ready_at_ms_{0};
    uint32_t timeout_at_ms_{0};
};

}

#endif
