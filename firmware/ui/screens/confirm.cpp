#include "ui/screens/confirm.h"

#include "core/util/format.h"

namespace skyblip::ui {

namespace {

constexpr int kLeft = kConfirmLeftX;
constexpr int kCellW = kConfirmCellW;

int length(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

// One line of at most kConfirmDetailCols characters, broken on a space so a
// sentence a pilot has to read in a hurry does not split a word.
int take_line(const char* text, int from, char* out) {
    const int len = length(text);
    if (from >= len) {
        out[0] = 0;
        return from;
    }
    int end = from + kConfirmDetailCols;
    if (end >= len) {
        end = len;
    } else {
        int space = end;
        while (space > from && text[space] != ' ') space--;
        if (space > from) end = space;
    }
    int n = 0;
    for (int i = from; i < end; i++) out[n++] = text[i];
    out[n] = 0;
    return text[end] == ' ' ? end + 1 : end;
}

}  // namespace

void draw_confirm(Framebuffer& fb, const ConfirmSnapshot& s) {
    fb.clear(true);

    // Reversed out across the full width: whatever page was up, this cannot be
    // mistaken for one of them at arm's length.
    fb.rect(0, 0, Framebuffer::kW, 26, true, /*fill=*/true);
    fb.draw_text(kLeft + kCellW, kConfirmHeaderY, "AUTHORISE", false, 2);

    fb.draw_text(kLeft, kConfirmTitleY, s.title, true, 2);
    fb.hline(kLeft, kConfirmTitleY + 22, Framebuffer::kW - 2 * kLeft, true);

    char line[kConfirmDetailCols + 1];
    int at = 0;
    for (int row = 0; row < kConfirmDetailRows; row++) {
        at = take_line(s.detail, at, line);
        if (line[0] == 0) break;
        fb.draw_text(kLeft, confirm_detail_y(row), line, true, 1);
    }

    // The allowing gesture is reversed out and the refusing one is not, so the
    // two are told apart by shape before either is read.
    const int w = (length(kConfirmAllowText) + 2) * kCellW;
    fb.rect(kLeft, kConfirmAllowY - 4, w, 15, true, /*fill=*/true);
    fb.draw_text(kLeft + kCellW, kConfirmAllowY, kConfirmAllowText, false, 1);
    fb.draw_text(kLeft + kCellW, kConfirmRefuseY, kConfirmRefuseText, true, 1);

    if (s.timeout_s == 0) return;
    char buf[32];
    int n = fmt_string(buf, "REFUSED AFTER ");
    n += fmt_uint(buf + n, s.timeout_s);
    n += fmt_string(buf + n, " S");
    buf[n] = 0;
    fb.draw_text(kLeft, kConfirmExpiryY, buf, true, 1);
}

}  // namespace skyblip::ui
