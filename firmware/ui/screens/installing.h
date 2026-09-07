#ifndef SKYBLIP_UI_SCREENS_INSTALLING_H
#define SKYBLIP_UI_SCREENS_INSTALLING_H

#include "ui/framebuffer.h"

namespace skyblip::ui {

constexpr int kInstallingLeftX = 4;
constexpr int kInstallingCellW = 6;
constexpr int kInstallingTitleY = 40;
constexpr int kInstallingLineH = 14;
constexpr int kInstallingBodyY = 90;

constexpr const char* kInstallingHeader = "FIRMWARE";
constexpr const char* kInstallingTitle = "INSTALLING";
constexpr const char* kInstallingBody[] = {"LEAVE THE DEVICE ON", "IT RESTARTS BY ITSELF",
                                           "IN ABOUT 30 S", "THE SCREEN STAYS STILL"};
constexpr int kInstallingBodyRows = 4;

constexpr int installing_body_y(int row) { return kInstallingBodyY + row * kInstallingLineH; }

void draw_installing(Framebuffer& fb);

}  // namespace skyblip::ui

#endif
