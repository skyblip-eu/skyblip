// The authorisation prompt, on the glass. Nothing sensitive the companion link
// asks for happens without this page having been read: it names the operation,
// says in a sentence what confirming it will do, and states the gesture that
// allows it next to the gesture that refuses it. A pilot who cannot tell from
// the panel what they are about to authorise has not authorised anything.
#ifndef SKYBLIP_UI_SCREENS_CONFIRM_H
#define SKYBLIP_UI_SCREENS_CONFIRM_H

#include <cstdint>

#include "ui/framebuffer.h"

namespace skyblip::ui {

struct ConfirmSnapshot {
    const char* title{""};
    const char* detail{""};
    // How long the prompt stands before it refuses on its own. Drawn as a
    // sentence, never as a running clock: a second-by-second countdown would
    // repaint the e-paper thirty times for one question.
    uint32_t timeout_s{0};
};

// The grid, exported so a test reads a line back off the glass instead of
// counting ink: "the page says POWER OFF" is the claim that matters.
constexpr int kConfirmLeftX = 4;
constexpr int kConfirmCellW = 6;
constexpr int kConfirmHeaderY = 6;
constexpr int kConfirmTitleY = 40;
constexpr int kConfirmDetailY = 74;
constexpr int kConfirmLineH = 11;
constexpr int kConfirmDetailCols = 30;
constexpr int kConfirmDetailRows = 3;
constexpr int kConfirmAllowY = 130;
constexpr int kConfirmRefuseY = 148;
constexpr int kConfirmExpiryY = 172;

constexpr int confirm_detail_y(int row) { return kConfirmDetailY + row * kConfirmLineH; }

// The two halves of the gesture, spelled out. They are constants because the
// page and its test must not be able to disagree about what the device accepts.
constexpr const char* kConfirmAllowText = "PRESS TWICE TO ALLOW";
constexpr const char* kConfirmRefuseText = "ONE PRESS REFUSES";

void draw_confirm(Framebuffer& fb, const ConfirmSnapshot& snapshot);

}  // namespace skyblip::ui

#endif
