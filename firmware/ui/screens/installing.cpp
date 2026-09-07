#include "ui/screens/installing.h"

namespace skyblip::ui {

void draw_installing(Framebuffer& fb) {
    fb.clear(true);
    fb.rect(0, 0, Framebuffer::kW, 26, true, /*fill=*/true);
    fb.draw_text(kInstallingLeftX + kInstallingCellW, 6, kInstallingHeader, false, 2);

    fb.draw_text(kInstallingLeftX, kInstallingTitleY, kInstallingTitle, true, 2);
    fb.hline(kInstallingLeftX, kInstallingTitleY + 22, Framebuffer::kW - 2 * kInstallingLeftX,
             true);

    for (int row = 0; row < kInstallingBodyRows; row++)
        fb.draw_text(kInstallingLeftX, installing_body_y(row), kInstallingBody[row], true, 1);
}

}  // namespace skyblip::ui
