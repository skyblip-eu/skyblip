// The status dump on the running product: where each number comes from, how often
// it leaves, and the fact that it only leaves at all when somebody has opened the
// port.
//
// Nothing below the services is stubbed. The receiver's baud rate, its firmware
// string and its refusals come off the same L76K driver the silicon build runs,
// through the board; the radio's floor and duty come off the radio service; the
// gauge's discarded readings come off the cutoff monitor inside the power service.
// That is the point of the case: the collector is the only thing on the device that
// reads any of them, so if a wire is missing here, nobody notices in the field
// either.
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "products/skyblip_go/services/diagnostics.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

// The USB console, as far as this suite is concerned: what was written, and
// whether a host had raised DTR when it was.
struct Capture {
    bool attached{true};
    std::vector<std::string> lines;

    bool ready() const { return attached; }
    void line(const char* text, int len) { lines.emplace_back(text, static_cast<size_t>(len)); }

    std::string all() const {
        std::string joined;
        for (const std::string& line : lines) joined += line + "\n";
        return joined;
    }
    // The MOST RECENT line for a subsystem. Every dump prints all five, so the
    // first one is always the dump the device made before it knew anything.
    std::string with(const char* prefix) const {
        for (auto it = lines.rbegin(); it != lines.rend(); ++it)
            if (it->rfind(prefix, 0) == 0) return *it;
        return std::string();
    }
};

using Dump = go::DiagnosticsDump<Go, Capture>;

bool has(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

// The dump is driven from the shell with the product in hand, once per pass, so
// this is what a pass looks like.
void pass(Rig& rig, Dump& dump, uint32_t& t, uint32_t ms) {
    const uint32_t until = t + ms;
    for (; t <= until; t += 50) {
        rig.platform.clock().set_millis(t);
        rig.product.step(t);
        dump.step(rig.product, t);
    }
}

// Seconds of flight: a solution a second, and the passes between them.
void fly(Rig& rig, Dump& dump, uint32_t& t, uint32_t seconds) {
    for (uint32_t i = 0; i < seconds; i++) {
        rig.push_timed_fix(200, 500);
        pass(rig, dump, t, 950);
        rig.utc_offset_s++;
    }
}

std::string last_config_frame(Rig& rig) {
    const auto& sent = rig.platform.link().sent;
    std::string joined;
    for (const auto& f : sent)
        if (f.endpoint == messages::Endpoint::Config) joined += f.bytes;
    return joined;
}

}  // namespace

TEST_CASE("diagnostics: the dump reads the device, subsystem by subsystem") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    Dump dump;
    uint32_t t = 0;
    // Past the second dump: the receiver has been woken, identified and
    // configured, the radio has armed dwells, and consecutive fixes have produced
    // a residual. The first dump is at boot and knows none of that yet, which is
    // why every assertion below reads the LAST line for its subsystem.
    fly(rig, dump, t, 12);

    const Capture& console = dump.sink();
    REQUIRE(console.lines.size() >= 10);

    // The receiver's own answers, which nothing else on this device reads.
    const std::string gnss = console.with("gnss ");
    REQUIRE_FALSE(gnss.empty());
    CHECK(has(gnss, "baud=9600"));  // parts::L76k::kBaudRate, unless autobaud moved
    CHECK(has(gnss, "identified=true"));
    CHECK(has(gnss, "firmware=\"URANUS5,V5.1.0.0\""));
    CHECK_FALSE(has(gnss, "fixes=0"));

    // And the refusal, named. In this rig the solutions are injected on the bus
    // while the part model itself says nothing, so the driver's own validity gate
    // is refusing every poll for want of an RMC - which is exactly the field case
    // this field exists for: a receiver that is wired, awake, answering commands
    // and producing no solutions used to be invisible from outside the device.
    CHECK(has(gnss, "reject=\"NO RMC\""));
    CHECK_FALSE(has(gnss, "rejected=0"));

    // The radio's discipline: the measured floor, the threshold in force, and the
    // hour's allowance spent so far.
    const std::string radio = console.with("radio ");
    REQUIRE_FALSE(radio.empty());
    CHECK(has(radio, "noise_dbm=-"));
    CHECK(has(radio, "lbt_dbm=-"));
    CHECK(has(radio, "duty_permille="));
    CHECK(has(radio, "gave_up=0"));
    CHECK(has(radio, "range_refused=0"));

    // And the two the shell already knew and could not say out loud.
    CHECK(has(console.with("sys "), "reset=\"POWER ON\""));
    CHECK(has(console.with("power "), "supply_warnings=0"));
    CHECK(has(console.with("power "), "implausible=0"));
    CHECK(has(console.with("traffic "), "tracked=0"));
}

// The counters that only move when something is wrong. Each one is somebody else's
// and none of them had a reader before the dump: a POFCON firing, a gauge reading
// the sanity floor threw away, and a reception the range gate refused.
TEST_CASE("diagnostics: the faults nobody could see reach the console") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    Dump dump;
    uint32_t t = 0;
    pass(rig, dump, t, 1000);

    // A collapsing rail, as the comparator reports it, and a divider that is not
    // connected: core/power counts both and acts on neither.
    rig.platform.system_power().supply_warning = true;
    rig.platform.battery().millivolts = 900;
    pass(rig, dump, t, 12000);

    const std::string power = dump.sink().with("power ");
    REQUIRE_FALSE(power.empty());
    CHECK(has(power, "supply_warnings=1"));
    CHECK_FALSE(has(power, "implausible=0"));
}

TEST_CASE("diagnostics: nothing is written until a host has opened the port") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    Dump dump;
    dump.sink().attached = false;
    uint32_t t = 0;
    pass(rig, dump, t, 25000);

    // Not a byte, for 25 seconds: a device in a flight bag does not talk to
    // nobody. The snapshot was still collected, so the phone that IS there gets a
    // fresh answer.
    CHECK(dump.sink().lines.empty());
    CHECK(rig.product.config().config().diagnostics().refreshes > 20);

    rig.raise_link();
    rig.send("{\"cmd\":\"diag\"}");
    pass(rig, dump, t, 200);
    CHECK(has(last_config_frame(rig), "\"group\":\"radio\""));
}

// Ten seconds is OGN's console cadence and a bench can read one dump before the
// next arrives. The snapshot behind it is refreshed every second, because the link
// may ask at any instant and a fix lost nine seconds ago is not news.
TEST_CASE("diagnostics: one dump every ten seconds, one refresh every second") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    Dump dump;
    uint32_t t = 0;

    // The first pass dumps, so a bench watching a device come up sees the reset
    // reason without waiting ten seconds for it.
    pass(rig, dump, t, 0);
    const size_t first = dump.sink().lines.size();
    CHECK(first == 5);
    const uint32_t refreshes = rig.product.config().config().diagnostics().refreshes;
    CHECK(refreshes == 1);

    pass(rig, dump, t, 5000);
    CHECK(dump.sink().lines.size() == first);
    CHECK(rig.product.config().config().diagnostics().refreshes >= refreshes + 5);

    pass(rig, dump, t, 5100);
    CHECK(dump.sink().lines.size() == 2 * first);
}

// One snapshot, two surfaces. A laptop on the bench and a phone on the wing report
// the same device or the dump is worse than nothing.
TEST_CASE("diagnostics: the console and the link cannot disagree") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    Dump dump;
    uint32_t t = 0;
    rig.platform.battery().millivolts = 3812;
    for (int i = 0; i < 4; i++) {
        rig.push_timed_fix(200, 500);
        pass(rig, dump, t, 950);
        rig.utc_offset_s++;
    }
    rig.raise_link();
    rig.platform.link().clear();
    dump.sink().lines.clear();
    // One dump and one question, close enough together that nothing between them
    // could have moved a counter: the ten-second cadence means the console's last
    // dump is otherwise older than the link's answer.
    pass(rig, dump, t, comms::kDiagnosticsDumpMs);
    rig.send("{\"cmd\":\"diag\"}");
    pass(rig, dump, t, 200);

    const std::string frames = last_config_frame(rig);
    const std::string console = dump.sink().all();
    REQUIRE_FALSE(frames.empty());

    // The state of charge the tablet draws is the state of charge the console
    // prints, digit for digit, because there is one snapshot behind both.
    const uint8_t percent = rig.state().battery.percent;
    const std::string key = std::to_string(percent);
    CHECK(has(frames, ("\"percent\":" + key).c_str()));
    CHECK(has(console, ("percent=" + key).c_str()));

    const std::string fixes = std::to_string(rig.state().gnss_fixes);
    CHECK(has(frames, ("\"fixes\":" + fixes).c_str()));
    CHECK(has(console, ("fixes=" + fixes).c_str()));
}

// M. The whole product, stepped through the instant hal::Clock::millis() turns
// over. Two things are being pinned. One: the collector's own cadences are
// unsigned differences, so the wrap costs one late dump and nothing else - a
// refresh cadence that broke here would freeze every number the dump carries
// while the device kept flying. Two: up_s is now_ms / 1000, so it counts 49 days
// and then starts again at zero, which is the honest answer. What would not be
// honest is a negative age or a four-billion-second jump, and neither can happen
// while the arithmetic is a division of the counter rather than a subtraction of
// two instants.
namespace {
// The rig's own pass() helper cannot be used here: it walks t up to t + ms, which
// is not a comparison that survives the wrap. The wrap is a value, not a wait.
void pass_across_wrap(Rig& rig, Dump& dump, uint32_t& t, uint32_t ms) {
    for (uint32_t stepped = 0; stepped <= ms; stepped += 50) {
        rig.platform.clock().set_millis(t);
        rig.product.step(t);
        dump.step(rig.product, t);
        t += 50;
    }
}
}  // namespace

TEST_CASE("diagnostics: the cadence and the uptime survive the 49.7-day wrap") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    Dump dump;
    uint32_t t = 0xFFFFFF00u;  // 256 ms short of the wrap

    // The first pass always dumps, and it dumps 49.7 days of uptime.
    pass_across_wrap(rig, dump, t, 0);
    REQUIRE(dump.sink().lines.size() == 5);
    CHECK(has(dump.sink().with("sys"), "up_s=4294967"));

    // Twenty seconds of passes, straddling zero: two more dumps, ten seconds
    // apart, exactly as on any other day.
    const uint32_t refreshes_before = rig.product.config().config().diagnostics().refreshes;
    pass_across_wrap(rig, dump, t, 20000);
    CHECK(dump.sink().lines.size() == 15);
    CHECK(rig.product.config().config().diagnostics().refreshes >= refreshes_before + 20);

    // And the uptime is small, positive, and counting again from the wrap.
    const uint32_t uptime_s = rig.product.config().config().diagnostics().uptime_s;
    CHECK(uptime_s < 60u);
    const std::string sys = dump.sink().with("sys");
    CHECK_FALSE(has(sys, "up_s=-"));
    CHECK_FALSE(has(sys, "up_s=4294"));
}
