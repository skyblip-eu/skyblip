// devices/drivers/ssd1681.h — 1.54" 200x200 e-paper (GDEH0154D67 / SSD1681)
#ifndef SKYBLIP_DEVICES_DRIVERS_SSD1681_H
#define SKYBLIP_DEVICES_DRIVERS_SSD1681_H

#include "devices/io/io.h"
#include "hal/display.h"

namespace skyblip::drivers {

class Ssd1681 : public hal::Display {
   public:
    Ssd1681(io::Spi& spi, io::Gpio& gpio, int dc, int rst, int busy)
        : spi_(spi), gpio_(gpio), dc_(dc), rst_(rst), busy_(busy) {}

    void begin();
    void present(const ui::Framebuffer& fb, hal::Rect region, hal::Refresh mode) override;
    void power_off() override;

    static constexpr int kFullRefreshEvery = 12;

   private:
    void cmd(uint8_t c);
    void data(uint8_t d);
    void set_window(int x0, int y0, int x1, int y1);
    void set_cursor(int x, int y);
    void wait_busy(uint32_t max_spins = 200000);

    io::Spi& spi_;
    io::Gpio& gpio_;
    int dc_, rst_, busy_;
    int partials_since_full_{0};
};

}

#endif
