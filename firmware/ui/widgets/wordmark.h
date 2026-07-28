#ifndef SKYBLIP_UI_WIDGETS_WORDMARK_H
#define SKYBLIP_UI_WIDGETS_WORDMARK_H

#include "ui/framebuffer.h"

namespace skyblip::ui {

// The skyBlip wordmark, centred on (cx, cy): the site's display face and the
// mark over the dotless i, baked to 1 bit by scripts/make_wordmark.py.
void draw_wordmark(Framebuffer& fb, int cx, int cy);

int wordmark_width();
int wordmark_height();

}  // namespace skyblip::ui

#endif
