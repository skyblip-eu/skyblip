#ifndef SKYBLIP_UI_WIDGETS_SKYSHIP_H
#define SKYBLIP_UI_WIDGETS_SKYSHIP_H

#include "ui/framebuffer.h"

namespace skyblip::ui {

// skyShip, the ownship symbol, with its hot spot on (cx, cy). Every screen that
// says "this is you" draws this one, which is why it is a widget and not part of
// whichever screen happened to need it first.
void draw_skyship(Framebuffer& fb, int cx, int cy);

}  // namespace skyblip::ui

#endif
