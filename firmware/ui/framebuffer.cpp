#include "ui/framebuffer.h"

namespace skyblip::ui {

namespace {
struct Glyph {
    char c;
    uint8_t col[5];
};
const Glyph kFont[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}}, {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}}, {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}}, {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}}, {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}}, {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}}, {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}}, {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}}, {'E', {0x7F, 0x49, 0x49, 0x41, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x01, 0x01}}, {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}}, {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}}, {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}}, {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}}, {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}}, {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}}, {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}}, {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}}, {'W', {0x7F, 0x20, 0x18, 0x20, 0x7F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}}, {'Y', {0x03, 0x04, 0x78, 0x04, 0x03}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}}, {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}}, {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'/', {0x20, 0x10, 0x08, 0x04, 0x02}}, {'+', {0x08, 0x08, 0x3E, 0x08, 0x08}},
};
const uint8_t* find_glyph(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    for (const auto& g : kFont)
        if (g.c == c) return g.col;
    return kFont[0].col;
}
}

void Framebuffer::clear(bool white) {
    uint8_t v = white ? 0x00 : 0xFF;
    for (size_t i = 0; i < kBytes; i++) buf_[i] = v;
}

void Framebuffer::set_pixel(int x, int y, bool black) {
    if (x < 0 || x >= kW || y < 0 || y >= kH) return;
    uint8_t& b = buf_[y * kStride + (x >> 3)];
    uint8_t m = static_cast<uint8_t>(0x80 >> (x & 7));
    if (black)
        b |= m;
    else
        b &= static_cast<uint8_t>(~m);
}

bool Framebuffer::get_pixel(int x, int y) const {
    if (x < 0 || x >= kW || y < 0 || y >= kH) return false;
    return buf_[y * kStride + (x >> 3)] & (0x80 >> (x & 7));
}

void Framebuffer::hline(int x, int y, int w, bool black) {
    for (int i = 0; i < w; i++) set_pixel(x + i, y, black);
}
void Framebuffer::vline(int x, int y, int h, bool black) {
    for (int i = 0; i < h; i++) set_pixel(x, y + i, black);
}

void Framebuffer::line(int x0, int y0, int x1, int y1, bool black) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        set_pixel(x0, y0, black);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void Framebuffer::rect(int x, int y, int w, int h, bool black, bool fill) {
    if (fill) {
        for (int j = 0; j < h; j++) hline(x, y + j, w, black);
    } else {
        hline(x, y, w, black);
        hline(x, y + h - 1, w, black);
        vline(x, y, h, black);
        vline(x + w - 1, y, h, black);
    }
}

void Framebuffer::circle(int cx, int cy, int r, bool black, bool fill) {
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        if (fill) {
            hline(cx - x, cy + y, 2 * x + 1, black);
            hline(cx - x, cy - y, 2 * x + 1, black);
            hline(cx - y, cy + x, 2 * y + 1, black);
            hline(cx - y, cy - x, 2 * y + 1, black);
        } else {
            set_pixel(cx + x, cy + y, black);
            set_pixel(cx - x, cy + y, black);
            set_pixel(cx + x, cy - y, black);
            set_pixel(cx - x, cy - y, black);
            set_pixel(cx + y, cy + x, black);
            set_pixel(cx - y, cy + x, black);
            set_pixel(cx + y, cy - x, black);
            set_pixel(cx - y, cy - x, black);
        }
        y++;
        if (err < 0)
            err += 2 * y + 1;
        else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

int Framebuffer::draw_char(int x, int y, char c, bool black, int scale) {
    const uint8_t* g = find_glyph(c);
    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            if (g[col] & (1 << row)) {
                for (int sx = 0; sx < scale; sx++)
                    for (int sy = 0; sy < scale; sy++)
                        set_pixel(x + col * scale + sx, y + row * scale + sy, black);
            }
        }
    }
    return x + 6 * scale;
}

int Framebuffer::draw_text(int x, int y, const char* s, bool black, int scale) {
    while (*s) {
        x = draw_char(x, y, *s++, black, scale);
    }
    return x;
}

int Framebuffer::count_black() const {
    int n = 0;
    for (size_t i = 0; i < kBytes; i++) n += __builtin_popcount(buf_[i]);
    return n;
}

}
