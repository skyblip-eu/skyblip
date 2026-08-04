// core/comms/diagnostics.h: the one status dump, and the only place its fields
// are named.
//
// Everything in it was already measured somewhere on the device and reachable
// from nowhere: the noise floor and the duty cycle in the radio service, rx_ok /
// rx_bad / tx_ok / tx_busy in bus::State, the range gate's refusals in the
// traffic table, POFCON firings and the gauge's discarded readings in the cutoff
// monitor, and the receiver's own answers in the L76K driver. A support case
// opens with these numbers, so they leave the device two ways - one line per
// subsystem on the USB console, and the same fields as JSON over the companion
// link - and both surfaces are rendered from ONE table below. A laptop and a
// phone that disagree about a counter are worse than neither having it.
//
// Not a shell and not OGN's thirty-five keys: five subsystems, thirty numbers,
// every one of them already computed. Nothing here is reset by being read.
#ifndef SKYBLIP_CORE_COMMS_DIAGNOSTICS_H
#define SKYBLIP_CORE_COMMS_DIAGNOSTICS_H

#include <cstdint>

#include "core/gnss/validity.h"
#include "core/power/battery.h"
#include "core/power/cutoff.h"
#include "core/power/reset_reason.h"
#include "core/timing/channel.h"

namespace skyblip::comms {

// What the dump is made of. A plain snapshot with two writers and no logic: the
// product's collector fills the first half from the services and the board
// (products/skyblip_go/services/diagnostics.h), and comms::ConfigService fills
// the second half, because the product already tells it those and a second copy
// of a number is a number that can be wrong in one place.
struct Diagnostics {
    // Collected from the product. Counted rather than flagged: it is the proof
    // that the collector is wired at all, and a dump of zeros from a device whose
    // wiring was forgotten would read as a dead radio and a dead receiver. The
    // link refuses to answer until it has moved.
    uint32_t refreshes{0};
    uint32_t uptime_s{0};

    int8_t noise_dbm{timing::NoiseFloor::kSeedDbm};
    int8_t lbt_dbm{timing::NoiseFloor::kSeedDbm + timing::NoiseFloor::kClearMarginDb};
    uint32_t duty_permille{0};
    uint32_t gave_up{0};
    uint32_t rx_ok{0};
    uint32_t rx_bad{0};
    uint32_t tx_ok{0};
    uint32_t tx_busy{0};

    uint32_t tracked{0};
    uint8_t alarm{0};

    uint32_t gnss_fixes{0};
    bool fix_valid{false};
    uint32_t gnss_baud{0};
    bool gnss_identified{false};
    // Points at the driver's own buffer, which outlives every reader of this.
    const char* gnss_firmware{""};
    gnss::FixReject gnss_reject{gnss::FixReject::None};
    uint32_t gnss_rejected{0};
    // The extrapolation the transmitter applies, measured against the fix that
    // closed the interval (products/skyblip_go/services/ownship.cpp). It belongs
    // to the fix stream that feeds it, which is why it reads out with the
    // receiver rather than with the radio.
    uint16_t resid_m{0};
    bool resid_valid{false};

    uint32_t supply_warnings{0};
    uint32_t battery_implausible{0};

    // Held by comms::ConfigService, which is told each of these by the product.
    power::ResetReason reset{power::ResetReason::Unknown};
    uint32_t link_drops{0};
    uint32_t range_refused{0};
    power::BatteryState battery{};
    power::PowerLevel level{power::PowerLevel::Unknown};
    int16_t die_decicelsius{0};
    bool die_valid{false};
};

// Why the last GNSS solution was refused, as a word. core/gnss spells no name for
// its own enum and this is the only surface that needs one; the day it grows a
// to_string, this delegates to it.
const char* reject_name(gnss::FixReject reason);

// Tenths of a degree as the whole degrees a support case is read in. The port
// carries tenths because the sensor resolves 0.25 C and rounding a sensor's
// resolution away at the port is not the port's decision. Rounded away from zero
// on both sides, so -20.6 C does not read as -20. One implementation, because the
// status reply and the dump must not round a support case differently.
int whole_celsius(int16_t decicelsius);

// The dump, rendered. Built cheaply on the stack from a snapshot, read either as
// console lines or as link frames, and then thrown away.
class DiagnosticsReport {
   public:
    enum class Group : uint8_t { Sys, Radio, Traffic, Gnss, Power };
    static constexpr int kGroupCount = 5;

    // The widest console line any group can produce at its widest values, plus
    // its terminator. test/core/test_diagnostics.cpp measures it rather than
    // trusting it.
    static constexpr int kLineCap = 208;
    // Enough for one frame at its widest on a link that can carry a whole group;
    // a frame is never longer than the negotiated payload, this only bounds the
    // buffer the caller lends.
    static constexpr int kFrameCap = 320;

    // cmd is the name the reply answers under, because the link already has a
    // question that asks for the radio group alone.
    DiagnosticsReport(const Diagnostics& diagnostics, const char* cmd);
    DiagnosticsReport(const Diagnostics& diagnostics, const char* cmd, Group only);

    // The console surface: one subsystem per line, "radio noise_dbm=-105 ...".
    int line_count() const { return line_count_; }
    // Writes line `index`, NUL-terminated, and returns its length. 0 when the
    // index is past the end or the buffer cannot hold the line whole - a line cut
    // in half is a counter misread, so it is refused instead.
    int line(int index, char* buf, int cap) const;

    // The link surface: as many complete flat objects as the negotiated payload
    // needs, each carrying "cmd", "group", "part" and "more". A frame never mixes
    // two subsystems, so a reader that only wants one can stop at its group.
    bool fits(int payload) const;
    bool exhausted() const { return at_ >= count_; }
    int next_frame(int payload, char* buf, int cap);

   private:
    enum class Kind : uint8_t { Int, Bool, Text };

    struct Field {
        const char* key;
        const char* text;
        long value;
        Kind kind;
        Group group;
    };

    static constexpr int kMaxFields = 34;

    void build(const Diagnostics& diagnostics, const Group* only);
    void add_int(Group group, const char* key, long value);
    void add_bool(Group group, const char* key, bool value);
    void add_text(Group group, const char* key, const char* value);
    static int field_bytes(const Field& field);
    static const char* group_name(Group group);
    // Where the index-th line's fields start, or count_ when there is no such
    // line.
    int line_start(int index) const;

    const char* cmd_;
    Field fields_[kMaxFields]{};
    int count_{0};
    int line_count_{0};
    int at_{0};
    int part_{0};
};

// INFO: fc 05aug26 The snapshot is refreshed once a second and dumped once every
// ten, which is OGN's own console cadence (oss/nrf52-ogn-tracker
// src/ogn-radio.cpp:1559-1573). Ten seconds is slow enough that a bench eye can
// read one dump before the next arrives and fast enough that a counter climbing
// under a fault is visible while the fault is still happening. It costs a battery
// nothing by construction: the console writer only emits when the CDC host has
// raised DTR, and a USB host is also 5 V on the same connector. The link half is
// pull-only, so a phone pays for exactly the dumps it asks for.
//
// The refresh is the cheaper half (thirty loads and four accessor calls) and is
// separate because the link may ask at any instant: a "diag" answered from a
// ten-second-old snapshot would report a fix that was lost nine seconds ago.
constexpr uint32_t kDiagnosticsRefreshMs = 1000;
constexpr uint32_t kDiagnosticsDumpMs = 10000;

}  // namespace skyblip::comms

#endif
