#ifndef SKYBLIP_HARDWARE_PARTS_SSD1681_H
#define SKYBLIP_HARDWARE_PARTS_SSD1681_H

#include "hal/display.h"
#include "hardware/io/io.h"

namespace skyblip::parts {

class Ssd1681 : public hal::Display {
   public:
    Ssd1681(io::Spi& spi, io::Gpio& gpio, int dc, int rst, int busy, int backlight = -1)
        : spi_(spi), gpio_(gpio), dc_(dc), rst_(rst), busy_(busy), backlight_(backlight) {}

    void begin();
    void present(const ui::Framebuffer& fb, hal::Rect region, hal::Refresh mode) override;
    void power_off() override;
    void power_on() override { begin(); }
    void set_backlight(bool on) override;

    static constexpr int kFullRefreshEvery = 12;

   private:
    void cmd(uint8_t c);
    void data(uint8_t d);
    void set_window(int x0, int y0, int x1, int y1);
    void set_cursor(int x, int y);
    void wait_busy(uint32_t max_spins = 200000);

    io::Spi& spi_;
    io::Gpio& gpio_;
    int dc_, rst_, busy_, backlight_;
    int partials_since_full_{0};
};

}

#endif
