// Which e-paper is actually glued to this board.
//
// LilyGO fits more than one panel behind the same footprint and the same
// controller pinout, and the panels do not behave the same. SoftRF fingerprints
// them by clocking registers 0x2D (11 bytes) and 0x2E (10 bytes) out of the
// panel over a bit-banged half-duplex SPI inside a critical section, and carries
// the signatures it has observed on real T-Echos in a comment block
// (platform/nRF52.cpp:3775-3900, table at 3855-3878). That table is transcribed
// below, byte for byte, including the two entries whose meaning it does not
// establish.
//
// The quirk that makes this worth doing at all is in the driver, not the ident:
// "SYX 1942 revision of D67 display can use power_off() after partial update,
// SYX 1948 revision - can not" (src/driver/EPD.cpp:861-865). SoftRF resolved it
// by never powering off after a partial update on any panel, because it cannot
// tell 1948 apart - there is no signature for 1948 in the table. So: an
// identified 1942 may sleep after a fast refresh, and everything else, unknown
// included, may not.
#ifndef SKYBLIP_HARDWARE_PARTS_SSD1681_PANEL_H
#define SKYBLIP_HARDWARE_PARTS_SSD1681_PANEL_H

#include <cstdint>

namespace skyblip::parts {

// The two registers, and the byte counts SoftRF reads from each.
constexpr uint8_t kPanelIdRegisterA = 0x2D;
constexpr uint8_t kPanelIdRegisterB = 0x2E;
constexpr int kPanelIdBytesA = 11;
constexpr int kPanelIdBytesB = 10;
constexpr int kPanelSignatureBytes = kPanelIdBytesA + kPanelIdBytesB;

struct PanelSignature {
    uint8_t a[kPanelIdBytesA]{};  // register 0x2D
    uint8_t b[kPanelIdBytesB]{};  // register 0x2E
    // False when the fingerprint could not be taken at all. On silicon the read
    // needs the panel's data line reversed, which only the board port can do,
    // and a board that cannot do it says so here rather than reporting a panel.
    bool read{false};
};

// Every panel the reference has seen in a T-Echo, plus the two it saw and could
// not name. Unknown is not a failure: it is what our own board is expected to
// report until a bench read is written down, because the Plus is one of the two
// rows SoftRF left as a date string.
enum class Panel : uint8_t {
    Unknown,
    Gdeh0154D67Syx1942,
    Gdeh0154D67Syx2118,
    Gdeh0154D67Syx2129,
    Depg0150Bn,
    Gdep015Oc1,
    ElecrowM1,
};

namespace panels {

// 0x2D: FF x11. 0x2E: 00 x10.
// GDEP015OC1, the first-generation panel, on a different controller: our init
// sequence is wrong for it, so finding one is a procurement alarm. It is NOT
// allowed to change what the driver does, because all-0xFF is also what an
// unwired read line returns and this board's e-paper MISO is a placeholder - see
// the note below the table.
constexpr PanelSignature kGdep015Oc1{
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    true};

// The revision the power-off-after-partial quirk was discovered on, and the one
// revision that tolerates it.
constexpr PanelSignature kGdeh0154D67Syx1942{
    {0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x40, 0x00, 0x00, 0x00, 0x01},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    true};

// 2118 and 2129 are the same in 0x2D and differ only in 0x2E, which is why the
// ident needs both registers and why a table keyed on 0x2D alone would be wrong.
constexpr PanelSignature kGdeh0154D67Syx2118{
    {0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00},
    {0x00, 0x05, 0x00, 0x9A, 0x00, 0x55, 0x35, 0x37, 0x14, 0x0C},
    true};

constexpr PanelSignature kGdeh0154D67Syx2129{
    {0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    true};

// 0x2D: 00 x11. The one identification SoftRF's own code actually acts on
// (nRF52.cpp:3888-3897: all-zero 0x2D means DEPG0150BN, everything else falls
// back to D67).
constexpr PanelSignature kDepg0150Bn{
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, true};

// SoftRF's table calls this one "TBD (M1)": observed in an Elecrow ThinkNode M1,
// not named. Carried so a bench read that lands on it is recognised as a panel
// somebody has seen rather than as noise.
constexpr PanelSignature kElecrowM1{
    {0x00, 0x00, 0x40, 0x20, 0x10, 0x00, 0x40, 0x03, 0x00, 0x00, 0x00},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    true};

}  // namespace panels

// The Plus is the seventh row of SoftRF's table, and it has no bytes: both
// registers are annotated only with the string "20.05.21", a date, which is not
// a signature. So the panel on OUR board is expected to be Unknown until
// somebody reads it on a bench, and Unknown is why the safe policy exists rather
// than being a theoretical branch.
constexpr const char* kPanelPlusUnrecorded = "20.05.21";

// Why a bad read is safe rather than merely detectable: the only identity that
// changes what the driver does is SYX 1942, and reaching it takes an exact match
// on all 21 bytes. A floating data line reads all-0x00 or all-0xFF, both of which
// land on panels that take the conservative branch, and noise lands on Unknown,
// which takes it too. There is no reading of an unwired pin that produces the
// permissive answer.

inline bool panel_signature_equal(const PanelSignature& x, const PanelSignature& y) {
    for (int i = 0; i < kPanelIdBytesA; i++)
        if (x.a[i] != y.a[i]) return false;
    for (int i = 0; i < kPanelIdBytesB; i++)
        if (x.b[i] != y.b[i]) return false;
    return true;
}

inline Panel identify_panel(const PanelSignature& signature) {
    if (!signature.read) return Panel::Unknown;
    if (panel_signature_equal(signature, panels::kGdeh0154D67Syx1942))
        return Panel::Gdeh0154D67Syx1942;
    if (panel_signature_equal(signature, panels::kGdeh0154D67Syx2118))
        return Panel::Gdeh0154D67Syx2118;
    if (panel_signature_equal(signature, panels::kGdeh0154D67Syx2129))
        return Panel::Gdeh0154D67Syx2129;
    if (panel_signature_equal(signature, panels::kDepg0150Bn)) return Panel::Depg0150Bn;
    if (panel_signature_equal(signature, panels::kGdep015Oc1)) return Panel::Gdep015Oc1;
    if (panel_signature_equal(signature, panels::kElecrowM1)) return Panel::ElecrowM1;
    return Panel::Unknown;
}

// Short enough for the self-test row it is printed on: 200 px of 5x7 glass is 33
// characters wide and the row already carries a name and a verdict.
constexpr const char* panel_name(Panel panel) {
    switch (panel) {
        case Panel::Gdeh0154D67Syx1942: return "D67/1942";
        case Panel::Gdeh0154D67Syx2118: return "D67/2118";
        case Panel::Gdeh0154D67Syx2129: return "D67/2129";
        case Panel::Depg0150Bn: return "DEPG0150";
        case Panel::Gdep015Oc1: return "OC1";
        case Panel::ElecrowM1: return "M1";
        case Panel::Unknown: break;
    }
    return "UNKNOWN";
}

// The refresh policy that follows from the identification. One rule, and the
// unknown panel takes the same branch as the revision that cannot survive it.
constexpr bool panel_may_sleep_after_fast_refresh(Panel panel) {
    return panel == Panel::Gdeh0154D67Syx1942;
}

// Deliberately absent: anything that would let an identification REFUSE to
// drive the glass. GDEP015OC1 is a different controller and our init sequence is
// wrong for it, but its signature is all-0xFF, which is also what an unwired
// read line returns - and this board's e-paper MISO is a placeholder. A rule
// that withdrew the display on an all-0xFF read would blank the one page that
// can explain a dead device. An OC1 verdict is a procurement alarm to be read on
// the self-test page, not a runtime decision.

}  // namespace skyblip::parts

#endif
