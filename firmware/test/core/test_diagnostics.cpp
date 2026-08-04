// The status dump, formatted: one line per subsystem for a laptop on the USB
// console, and the same table as JSON frames for the companion page. Both
// surfaces come out of comms::DiagnosticsReport, so this is where it is proved
// that they carry the same numbers under the same names, that neither is ever
// truncated, and that reading one does not disturb anything.
//
// No device and no link here: a snapshot struct in, characters out.
#include <string>

#include "core/comms/config.h"  // kSmallestSupportedPayload: the ceiling both surfaces answer under
#include "core/comms/diagnostics.h"
#include "core/comms/frame_budget.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::comms;

namespace {

// A device that has been up for a while and has something to complain about, so
// that no field reads as its default.
Diagnostics busy_device() {
    Diagnostics d{};
    d.refreshes = 12;
    d.uptime_s = 3725;
    d.reset = power::ResetReason::Watchdog;
    d.link_drops = 2;

    d.noise_dbm = -101;
    d.lbt_dbm = -91;
    d.duty_permille = 7;
    d.gave_up = 3;
    d.rx_ok = 1204;
    d.rx_bad = 37;
    d.tx_ok = 880;
    d.tx_busy = 9;
    d.range_refused = 5;

    d.tracked = 4;
    d.alarm = 2;

    d.gnss_fixes = 5210;
    d.fix_valid = true;
    d.gnss_baud = 38400;
    d.gnss_identified = true;
    d.gnss_firmware = "URANUS5,V5.1.0.0";
    d.gnss_reject = gnss::FixReject::Stale;
    d.gnss_rejected = 6;
    d.resid_m = 13;
    d.resid_valid = true;

    d.battery.millivolts = 3812;
    d.battery.percent = 64;
    d.battery.valid = true;
    d.battery.charging = false;
    d.level = power::PowerLevel::Normal;
    d.supply_warnings = 1;
    d.battery_implausible = 2;
    d.die_decicelsius = 415;
    d.die_valid = true;
    return d;
}

// Every field at the widest it can be: every counter at the widest value the dump
// can carry, and the longest word every string field can hold. The counters are
// handed in wider than that on purpose - see the saturation case below.
Diagnostics widest_device() {
    Diagnostics d{};
    d.refreshes = 0xFFFFFFFFu;
    d.uptime_s = 0xFFFFFFFFu;
    d.reset = power::ResetReason::Lockup;  // "CPU LOCKUP"
    d.link_drops = 0xFFFFFFFFu;
    d.noise_dbm = -128;
    d.lbt_dbm = -128;
    d.duty_permille = 1000;
    d.gave_up = 0xFFFFFFFFu;
    d.rx_ok = 0xFFFFFFFFu;
    d.rx_bad = 0xFFFFFFFFu;
    d.tx_ok = 0xFFFFFFFFu;
    d.tx_busy = 0xFFFFFFFFu;
    d.range_refused = 0xFFFFFFFFu;
    d.tracked = 0xFFFFFFFFu;
    d.alarm = 255;
    d.gnss_fixes = 0xFFFFFFFFu;
    d.gnss_baud = 921600;
    // The driver's version buffer is 24 bytes, so 23 characters is the longest
    // string a receiver can ever hand us (core/gnss/nmea.h::kVersionCap).
    d.gnss_firmware = "ABCDEFGHIJKLMNOPQRSTUVW";
    d.gnss_reject = gnss::FixReject::NoSolution;  // "NO SOLUTION"
    d.gnss_rejected = 0xFFFFFFFFu;
    d.resid_m = 65535;
    d.resid_valid = true;
    d.battery.millivolts = 65535;
    d.battery.percent = 255;
    d.level = power::PowerLevel::Cutoff;  // "CUTOFF"
    d.supply_warnings = 0xFFFFFFFFu;
    d.battery_implausible = 0xFFFFFFFFu;
    d.die_decicelsius = -1250;
    d.die_valid = true;
    return d;
}

std::string console(const Diagnostics& d) {
    DiagnosticsReport report(d, "diag");
    std::string all;
    char line[DiagnosticsReport::kLineCap];
    for (int i = 0; i < report.line_count(); i++) {
        const int len = report.line(i, line, static_cast<int>(sizeof(line)));
        REQUIRE(len > 0);
        CHECK(static_cast<int>(std::string(line).size()) == len);
        all += line;
        all += "\n";
    }
    return all;
}

// Every frame the report produces at a given payload, joined, plus a count.
struct Frames {
    std::string joined;
    int count{0};
    int widest{0};
    std::string last;
};

Frames frames(const Diagnostics& d, int payload, const char* cmd = "diag") {
    DiagnosticsReport report(d, cmd);
    Frames out;
    char buf[DiagnosticsReport::kFrameCap];
    while (!report.exhausted()) {
        const int len = report.next_frame(payload, buf, static_cast<int>(sizeof(buf)));
        REQUIRE(len > 0);
        REQUIRE(len <= payload);
        out.joined += std::string(buf, static_cast<size_t>(len));
        out.last = std::string(buf, static_cast<size_t>(len));
        if (len > out.widest) out.widest = len;
        out.count++;
        REQUIRE(out.count < 32);  // a dump that never ends is a bug, not a report
    }
    return out;
}

bool has(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("diagnostics: one line per subsystem, each carrying the counters that already exist") {
    const std::string text = console(busy_device());

    // Five subsystems, in the order a bench eye wants them: how long it has been
    // up, then the radio, then what it heard, then what it knows about itself.
    CHECK(has(text, "sys up_s=3725 reset=\"WATCHDOG\" link_drops=2\n"));
    CHECK(has(text,
              "radio noise_dbm=-101 lbt_dbm=-91 duty_permille=7 gave_up=3 rx_ok=1204 rx_bad=37 "
              "tx_ok=880 tx_busy=9 range_refused=5\n"));
    CHECK(has(text, "traffic tracked=4 alarm=2\n"));
    CHECK(has(text,
              "gnss fixes=5210 valid=true baud=38400 identified=true "
              "firmware=\"URANUS5,V5.1.0.0\" reject=\"STALE\" rejected=6 resid_m=13\n"));
    CHECK(has(text,
              "power mv=3812 percent=64 valid=true charging=false level=\"OK\" supply_warnings=1 "
              "implausible=2 die_temp_c=42\n"));

    DiagnosticsReport report(busy_device(), "diag");
    CHECK(report.line_count() == DiagnosticsReport::kGroupCount);
    // Past the end is nothing at all, not an empty line a reader would parse.
    char line[DiagnosticsReport::kLineCap];
    CHECK(report.line(DiagnosticsReport::kGroupCount, line, sizeof(line)) == 0);
    CHECK(report.line(-1, line, sizeof(line)) == 0);
}

// A support case is pasted into an email, so the console form has to stay
// machine-readable: "CPU LOCKUP" is one value, not two fields.
TEST_CASE("diagnostics: a text value is quoted on both surfaces, spaces and all") {
    Diagnostics d = busy_device();
    d.reset = power::ResetReason::Lockup;
    CHECK(has(console(d), "reset=\"CPU LOCKUP\""));
    CHECK(has(frames(d, 182).joined, "\"reset\":\"CPU LOCKUP\""));
}

// The buffer the caller lends is a constant in the header, so the widest line
// this dump can ever produce has to be measured rather than assumed.
TEST_CASE("diagnostics: the widest dump every field can produce still fits one console line") {
    const Diagnostics d = widest_device();
    DiagnosticsReport report(d, "diag");
    int widest = 0;
    char line[DiagnosticsReport::kLineCap];
    for (int i = 0; i < report.line_count(); i++) {
        const int len = report.line(i, line, static_cast<int>(sizeof(line)));
        REQUIRE(len > 0);
        if (len > widest) widest = len;
    }
    CHECK(widest + 1 <= DiagnosticsReport::kLineCap);
    // And a buffer one byte short of the line refuses it whole rather than
    // handing back half a counter.
    char tight[DiagnosticsReport::kLineCap];
    DiagnosticsReport again(d, "diag");
    int longest_index = 0;
    for (int i = 0; i < again.line_count(); i++) {
        if (again.line(i, line, static_cast<int>(sizeof(line))) == widest) longest_index = i;
    }
    CHECK(again.line(longest_index, tight, widest) == 0);
}

TEST_CASE("diagnostics: no reading is no key, on the console as on the link") {
    Diagnostics d = busy_device();
    d.resid_valid = false;
    d.die_valid = false;

    const std::string text = console(d);
    const std::string json = frames(d, 182).joined;
    CHECK_FALSE(has(text, "resid_m"));
    CHECK_FALSE(has(text, "die_temp_c"));
    CHECK_FALSE(has(json, "resid_m"));
    CHECK_FALSE(has(json, "die_temp_c"));

    // 0 m of residual and 0 C of die temperature are both real readings a device
    // can produce, which is why absent is not spelled as zero.
    d.resid_valid = true;
    d.die_valid = true;
    d.resid_m = 0;
    d.die_decicelsius = 0;
    CHECK(has(console(d), "resid_m=0"));
    CHECK(has(console(d), "die_temp_c=0"));
    CHECK(has(frames(d, 182).joined, "\"resid_m\":0"));
    CHECK(has(frames(d, 182).joined, "\"die_temp_c\":0"));
}

// The rounding rule is the status reply's, and it is shared code rather than a
// second copy: a support case must not read a different temperature depending on
// which surface answered it.
TEST_CASE("diagnostics: tenths of a degree round away from zero on both sides of freezing") {
    CHECK(whole_celsius(-206) == -21);
    CHECK(whole_celsius(206) == 21);
    CHECK(whole_celsius(-4) == 0);
    CHECK(whole_celsius(0) == 0);
}

TEST_CASE("diagnostics: the receiver's own answers reach the dump, named not numbered") {
    Diagnostics d = busy_device();
    d.gnss_identified = false;
    d.gnss_firmware = "";
    d.gnss_baud = 9600;
    d.gnss_reject = gnss::FixReject::NoDate;
    d.gnss_rejected = 41;

    const std::string text = console(d);
    CHECK(has(text, "identified=false"));
    CHECK(has(text, "firmware=\"\""));
    CHECK(has(text, "baud=9600"));
    CHECK(has(text, "reject=\"NO DATE\""));
    CHECK(has(text, "rejected=41"));

    // Every reason core/gnss can refuse a solution for has a word, so a support
    // case reads a cause and not an enum value.
    CHECK(std::string(reject_name(gnss::FixReject::None)) == "NONE");
    CHECK(std::string(reject_name(gnss::FixReject::NoSolution)) == "NO SOLUTION");
    CHECK(std::string(reject_name(gnss::FixReject::MissingRmc)) == "NO RMC");
    CHECK(std::string(reject_name(gnss::FixReject::MissingGga)) == "NO GGA");
    CHECK(std::string(reject_name(gnss::FixReject::Stale)) == "STALE");
    CHECK(std::string(reject_name(gnss::FixReject::NoDate)) == "NO DATE");
    CHECK(std::string(reject_name(gnss::FixReject::Jump)) == "JUMP");
}

// The narrowest phone in the field carries 182 bytes of notification payload, and
// a frame one byte over that is not shortened by the controller - it fails. So the
// dump is cut into frames the payload can carry, and every frame is a complete
// object naming the subsystem it belongs to.
TEST_CASE("diagnostics: the link form is frames the negotiated payload can carry, whole") {
    const Diagnostics d = widest_device();
    const Frames narrow = frames(d, kSmallestSupportedPayload);
    CHECK(narrow.widest <= kSmallestSupportedPayload);
    CHECK(narrow.count > DiagnosticsReport::kGroupCount);  // the widest values need a second pass
    CHECK(has(narrow.last, "\"more\":false"));
    // Only the last frame says there is nothing after it.
    size_t at = 0;
    int finals = 0;
    while ((at = narrow.joined.find("\"more\":false", at)) != std::string::npos) {
        finals++;
        at += 1;
    }
    CHECK(finals == 1);

    // A link wide enough for everything answers in one frame per subsystem, and
    // never fewer: a reader that only wants the radio stops at its group.
    const Frames wide = frames(busy_device(), 512);
    CHECK(wide.count == DiagnosticsReport::kGroupCount);
    CHECK(has(wide.joined, "\"group\":\"sys\""));
    CHECK(has(wide.joined, "\"group\":\"radio\""));
    CHECK(has(wide.joined, "\"group\":\"traffic\""));
    CHECK(has(wide.joined, "\"group\":\"gnss\""));
    CHECK(has(wide.joined, "\"group\":\"power\""));
}

TEST_CASE("diagnostics: a frame never mixes two subsystems") {
    DiagnosticsReport report(widest_device(), "diag");
    char buf[DiagnosticsReport::kFrameCap];
    while (!report.exhausted()) {
        const int len = report.next_frame(kSmallestSupportedPayload, buf, sizeof(buf));
        REQUIRE(len > 0);
        const std::string body(buf, static_cast<size_t>(len));
        // The radio's keys cannot appear in a frame that says it is the receiver's,
        // whichever way round the report packed them.
        if (has(body, "\"group\":\"gnss\"")) {
            CHECK_FALSE(has(body, "rx_ok"));
            CHECK_FALSE(has(body, "noise_dbm"));
        }
        if (has(body, "\"group\":\"radio\"")) {
            CHECK_FALSE(has(body, "firmware"));
            CHECK_FALSE(has(body, "\"baud\""));
        }
    }
}

// All or nothing, asked before the first frame goes out: a bench holding four
// frames of a five-frame dump, with no fifth one coming, has been told that a
// fault it cannot see is absent.
TEST_CASE("diagnostics: a link too narrow for one field is refused, not answered short") {
    const Diagnostics d = widest_device();
    DiagnosticsReport report(d, "diag");
    CHECK(report.fits(kSmallestSupportedPayload));
    // hal::kMinimumLinkPayload, which is all BLE guarantees: not one worst-case
    // field plus the four keys that name the frame.
    CHECK_FALSE(report.fits(20));

    char buf[DiagnosticsReport::kFrameCap];
    DiagnosticsReport narrow(d, "diag");
    CHECK(narrow.next_frame(20, buf, sizeof(buf)) == 0);
    CHECK_FALSE(narrow.exhausted());
}

TEST_CASE("diagnostics: one subsystem can be asked for on its own, under its own name") {
    const Diagnostics d = busy_device();
    DiagnosticsReport report(d, "radio", DiagnosticsReport::Group::Radio);
    CHECK(report.line_count() == 1);

    char buf[DiagnosticsReport::kFrameCap];
    const int len = report.next_frame(kSmallestSupportedPayload, buf, sizeof(buf));
    REQUIRE(len > 0);
    const std::string body(buf, static_cast<size_t>(len));
    CHECK(has(body, "\"cmd\":\"radio\""));
    CHECK(has(body, "\"group\":\"radio\""));
    CHECK(has(body, "\"range_refused\":5"));
    // And nothing from any other subsystem came with it.
    CHECK_FALSE(has(body, "up_s"));
    CHECK_FALSE(has(body, "firmware"));
    CHECK(report.exhausted());
}

// A dump is a witness. A witness that consumes its evidence makes the second
// reader wrong, which is why every read in the collector is a const accessor and
// why two reports of the same snapshot are the same bytes.
// json::Writer carries a long, which is 64-bit on this host and 32-bit on the
// nRF52. A naked cast would therefore print a negative count on the device and a
// correct one right here, in the suite we look at. So it saturates, at a ceiling
// no device reaches: 2147483647 receptions is thirteen years of them at 5 Hz.
TEST_CASE("diagnostics: a counter is never printed negative, on any platform") {
    Diagnostics d{};
    d.rx_ok = 0xFFFFFFFFu;
    d.gnss_rejected = 0x80000000u;
    d.link_drops = 0x7FFFFFFFu;
    const std::string text = console(d);
    CHECK(has(text, "rx_ok=2147483647"));
    CHECK(has(text, "rejected=2147483647"));
    CHECK(has(text, "link_drops=2147483647"));
    // The dBm figures are signed and stay signed; a COUNT never is.
    CHECK(has(text, "noise_dbm=-105"));
    CHECK_FALSE(has(text, "rx_ok=-"));
    CHECK_FALSE(has(text, "rejected=-"));
    CHECK_FALSE(has(frames(d, 182).joined, "\"rx_ok\":-"));
}

TEST_CASE("diagnostics: reading the dump neither clears nor changes anything") {
    const Diagnostics d = busy_device();
    const std::string first = console(d);
    const std::string first_json = frames(d, 182).joined;
    const std::string second = console(d);
    const std::string second_json = frames(d, 182).joined;
    CHECK(first == second);
    CHECK(first_json == second_json);
    CHECK(d.rx_ok == 1204);
    CHECK(d.gave_up == 3);
    CHECK(d.supply_warnings == 1);
}

// M. The clamp itself, since both link reports now share it and neither of them
// can reach 2^31 of anything in a test: the dump above proves it through rx_ok,
// this proves the conversion, and core/comms/timing_report.cpp is the second
// caller. The ceiling is a saturation and not a wrap, so the widest count still
// costs ten characters in a frame that was sized before it was written.
TEST_CASE("diagnostics: the shared counter conversion saturates, and stays ten digits") {
    CHECK(frame::counter(0u) == 0);
    CHECK(frame::counter(1000u) == 1000);
    CHECK(frame::counter(0x7FFFFFFFu) == 2147483647L);
    CHECK(frame::counter(0x80000000u) == 2147483647L);
    CHECK(frame::counter(0xFFFFFFFFu) == 2147483647L);
    CHECK(frame::counter(0xFFFFFFFFu) > 0);
    CHECK(frame::number_bytes(frame::counter(0xFFFFFFFFu)) == 10);
}
