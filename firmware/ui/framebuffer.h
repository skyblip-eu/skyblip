// ui/framebuffer.h: 1-bit 200x200 framebuffer + minimal drawing primitives.
#ifndef SKYBLIP_UI_FRAMEBUFFER_H
#define SKYBLIP_UI_FRAMEBUFFER_H

#include <cstddef>
#include <cstdint>

namespace skyblip::ui {

class Framebuffer {
   public:
    static constexpr int kW = 200;
    static constexpr int kH = 200;
    static constexpr int kStride = (kW + 7) / 8;
    static constexpr size_t kBytes = kStride * kH;

    void clear(bool white = true);
    void set_pixel(int x, int y, bool black);
    bool get_pixel(int x, int y) const;

    void hline(int x, int y, int w, bool black);
    void vline(int x, int y, int h, bool black);
    void line(int x0, int y0, int x1, int y1, bool black);
    void rect(int x, int y, int w, int h, bool black, bool fill = false);
    void circle(int cx, int cy, int r, bool black, bool fill = false);

    int draw_char(int x, int y, char c, bool black, int scale = 1);
    int draw_text(int x, int y, const char* s, bool black, int scale = 1);

    const uint8_t* data() const { return buf_; }
    uint8_t* data() { return buf_; }
    int count_black() const;

   private:
    uint8_t buf_[kBytes]{};
};

}

#endif
