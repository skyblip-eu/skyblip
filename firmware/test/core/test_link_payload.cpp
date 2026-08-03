// What one frame may carry, and what each sender does when the answer is small.
//
// The finding this file exists for: the companion app worked on Android and not
// on an iPhone. Nothing read the MTU the central negotiated - every reply was
// sized against a local buffer, prj.conf asked for CONFIG_BT_L2CAP_TX_MTU=498
// and an iOS central commonly settles at 185, which is 182 bytes of notification
// payload. A notification longer than the payload is not truncated by the
// controller, it fails, so a reply that does not fit is either sized to fit or
// refused out loud - never handed down and hoped for.
//
// A link model and scripted JSON, no device: the product's own two ends of the
// range are walked in test/products/test_flight_log.cpp.
#include <cstring>
#include <string>

#include "core/comms/config.h"
#include "core/comms/log_link.h"
#include "core/comms/timing_report.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/link.h"
#include "runtime/null.h"

using namespace skyblip;
using namespace skyblip::comms;

namespace {

messages::RxFrame frame(const char* json) {
    messages::RxFrame f{};
    f.session_id = 1;
    f.endpoint = messages::Endpoint::Config;
    f.len = static_cast<uint16_t>(std::strlen(json));
    std::memcpy(f.data.data(), json, f.len);
    return f;
}

// The widest a unit can make its own answers: every field at the longest value
// it is allowed to hold, so these cases measure the worst case rather than a
// convenient one.
settings::Settings widest_settings() {
    settings::Settings s = settings::defaults(1);
    s.device_addr = 16777215;  // the whole 24-bit space: eight digits
    s.addr_table = 63;
    s.aircraft_type = 17;
    s.alarm_volume = 5;
    s.page_mask = 255;
    s.units = settings::Units::Imperial;
    std::memcpy(s.callsign, "ABCDEFGHI", 10);  // the nine characters the schema allows
    return s;
}

power::BatteryState full_battery() {
    power::BatteryState b{};
    b.millivolts = 4200;
    b.percent = 100;  // three digits
    b.external_power = true;
    b.charging = true;
    b.valid = true;
    return b;
}

// A device at its most talkative: the longest reset reason there is, the longest
// flight word, the longest power level, a full cell.
void make_worst_case(ConfigService& cs) {
    cs.set_reset_reason(power::ResetReason::Lockup);
    cs.set_flight_state(FlightState::Airborne);
    cs.set_battery_state(full_battery(), power::PowerLevel::Cutoff);
}

// Two weeks of bench: one PPS edge a second puts a bucket into seven figures,
// and a histogram that loses its last bucket is not evidence.
void run_bench(timing::SlotTimingStats& stats) {
    for (uint32_t i = 0; i < 1200000u; i++) {
        stats.record_edge(static_cast<uint64_t>(i) * 1000000u, true);
        stats.record_dwell_phase(static_cast<int64_t>(i % 7) * 3000 - 9000);
    }
    stats.record_missed();
    stats.record_refused();
}

std::string joined(const platform::host::Link& link) {
    std::string all;
    for (const platform::host::Link::Frame& f : link.sent) all += f.bytes;
    return all;
}

int occurrences(const std::string& haystack, const char* needle) {
    int n = 0;
    for (size_t at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + 1))
        n++;
    return n;
}

}  // namespace

TEST_CASE("link: a link that has not been told what it carries promises what BLE guarantees") {
    // 23 bytes of default ATT_MTU less the three of notification header. Both
    // upstream SoftRF forks stop here and never send more; we only do better
    // when a central has agreed to.
    CHECK(hal::kMinimumLinkPayload == 20);
    runtime::NullLink absent;
    CHECK(absent.payload_bytes() == hal::kMinimumLinkPayload);

    platform::host::Link link;
    link.declare_payload_bytes(4);
    CHECK(link.payload_bytes() == hal::kMinimumLinkPayload);

    // And a frame longer than that is refused rather than shortened, which is
    // what a controller does with an oversized notification.
    const char* twenty_one = "123456789012345678901";
    const ConstByteSpan too_long(reinterpret_cast<const uint8_t*>(twenty_one), 21);
    CHECK(link.send(messages::Endpoint::Config, too_long) == Status::OutOfRange);
    CHECK(link.sent.empty());
    CHECK(link.refused_oversize == 1);
}

TEST_CASE("comms: the status a phone is pushed fits the narrowest payload we support, worst case") {
    platform::host::Link link;
    link.declare_payload_bytes(kSmallestSupportedPayload);
    settings::Settings s = widest_settings();
    ConfigService cs(link, s);
    make_worst_case(cs);

    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    REQUIRE(link.sent.size() == 1);
    const std::string body = link.last().bytes;
    CHECK(body.size() <= static_cast<size_t>(kSmallestSupportedPayload));
    CHECK(link.refused_oversize == 0);
    CHECK(cs.link_drops() == 0);

    // Every field whole. json::Writer leaves a field out rather than cutting it,
    // so "fits" has to mean all of them and not merely valid JSON.
    CHECK(body.find("\"cmd\":\"status\"") != std::string::npos);
    CHECK(body.find("\"reset\":\"CPU LOCKUP\"") != std::string::npos);
    CHECK(body.find("\"flight\":\"airborne\"") != std::string::npos);
    CHECK(body.find("\"upload\":false") != std::string::npos);
    CHECK(body.find("\"battery_percent\":100") != std::string::npos);
    CHECK(body.find("\"battery_valid\":true") != std::string::npos);
    CHECK(body.find("\"charging\":true") != std::string::npos);
    CHECK(body.find("\"power_level\":\"CUTOFF\"") != std::string::npos);
    CHECK(body.back() == '}');
}

TEST_CASE("comms: the config reply fits it too, as one flat object instead of an escaped one") {
    platform::host::Link link;
    link.declare_payload_bytes(kSmallestSupportedPayload);
    settings::Settings s = widest_settings();
    ConfigService cs(link, s);

    cs.on_rx(frame("{\"cmd\":\"get\"}"));
    REQUIRE(link.sent.size() == 1);
    const std::string body = link.last().bytes;
    CHECK(body.size() <= static_cast<size_t>(kSmallestSupportedPayload));
    CHECK(cs.link_drops() == 0);

    // The settings used to ride as a JSON string inside the reply, and the
    // backslashes alone were 53 bytes of a 158-byte body. There is not one left.
    CHECK(body.find('\\') == std::string::npos);
    CHECK(body.find("\"cmd\":\"config\"") != std::string::npos);
    CHECK(body.find("\"version\":1") != std::string::npos);
    CHECK(body.find("\"addr\":16777215") != std::string::npos);
    CHECK(body.find("\"addr_table\":63") != std::string::npos);
    CHECK(body.find("\"callsign\":\"ABCDEFGHI\"") != std::string::npos);
    // The last field written, so its presence is the proof nothing was dropped.
    CHECK(body.find("\"page_mask\":255") != std::string::npos);
}

TEST_CASE("comms: a link that came up at the BLE minimum is answered with a count, not a frame") {
    platform::host::Link link;
    link.declare_payload_bytes(hal::kMinimumLinkPayload);
    settings::Settings s = widest_settings();
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);

    // Nothing in this dialect fits twenty bytes, not even a refusal, so the
    // honest answer is silence and a number - never a notification the
    // controller would fail, and never a reply with fields quietly missing.
    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    cs.on_rx(frame("{\"cmd\":\"get\"}"));
    cs.on_rx(frame("{\"cmd\":\"nonsense\"}"));
    CHECK(link.sent.empty());
    CHECK(cs.link_drops() == 3);
    // The refusal happened above the port: nothing was ever handed down.
    CHECK(link.refused_oversize == 0);
}

TEST_CASE("comms: the bench's timing report is one frame on a wide link and several on a narrow") {
    timing::SlotTimingStats stats;
    run_bench(stats);
    settings::Settings s = settings::defaults(1);

    // A phone that negotiated the whole L2CAP MTU: one frame, and it says so.
    platform::host::Link wide;
    wide.declare_payload_bytes(495);
    ConfigService on_wide(wide, s, nullptr, &stats);
    on_wide.on_rx(frame("{\"cmd\":\"timing\"}"));
    REQUIRE(wide.sent.size() == 1);
    CHECK(wide.last().bytes.find("\"part\":0") != std::string::npos);
    CHECK(wide.last().bytes.find("\"more\":false") != std::string::npos);

    // The same numbers to an iPhone: more than one frame, each one a complete
    // object no longer than the link said it would carry, and the same twelve
    // fields between them - a laboratory reads this once, so a second frame is
    // free, and trimming a bucket would destroy what it came for.
    platform::host::Link narrow;
    narrow.declare_payload_bytes(kSmallestSupportedPayload);
    ConfigService on_narrow(narrow, s, nullptr, &stats);
    on_narrow.on_rx(frame("{\"cmd\":\"timing\"}"));
    REQUIRE(narrow.sent.size() > 1);
    CHECK(narrow.sent.back().bytes.find("\"more\":false") != std::string::npos);
    for (size_t i = 0; i < narrow.sent.size(); i++) {
        const std::string& body = narrow.sent[i].bytes;
        CHECK(body.size() <= static_cast<size_t>(kSmallestSupportedPayload));
        CHECK(body.front() == '{');
        CHECK(body.back() == '}');
        CHECK(body.find("\"cmd\":\"timing\"") != std::string::npos);
        if (i + 1 < narrow.sent.size()) CHECK(body.find("\"more\":true") != std::string::npos);
    }

    const std::string all = joined(narrow);
    CHECK(occurrences(all, "\"pps_us\":") == 1);
    CHECK(occurrences(all, "\"dwell_us\":") == 1);
    CHECK(occurrences(all, "\"holdover\":") == 1);
    CHECK(occurrences(all, "\"carrier_sense_us\":") == 1);
    CHECK(all.find("\"pps_samples\":1199999") != std::string::npos);
    CHECK(all.find("\"missed\":1") != std::string::npos);
    CHECK(all.find("\"refused\":1") != std::string::npos);
    CHECK(on_narrow.link_drops() == 0);
    CHECK(narrow.refused_oversize == 0);

    // A link so narrow that one histogram cannot be placed in any frame at all
    // is refused whole: two frames of a three-frame report, with no third one
    // coming, is worse than no report.
    platform::host::Link hopeless;
    hopeless.declare_payload_bytes(64);
    ConfigService on_hopeless(hopeless, s, nullptr, &stats);
    on_hopeless.on_rx(frame("{\"cmd\":\"timing\"}"));
    CHECK(hopeless.sent.empty());
    CHECK(on_hopeless.link_drops() == 1);
}

TEST_CASE("comms: a status push the controller could not take is retried, not lost") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);
    cs.on_link_up(messages::LinkUp{1, link.payload_bytes()});

    // Out of buffers for one pass - an upload sharing the connection will do
    // that. The push is the one frame nobody asked for, so nobody will ask
    // again: it is the one that has to come back on the next pass.
    link.force_status(Status::WouldBlock);
    cs.set_battery_state(full_battery(), power::PowerLevel::Normal);
    CHECK(link.sent.empty());
    CHECK(cs.link_drops() == 1);

    cs.tick(10);
    REQUIRE(link.sent.size() == 1);
    CHECK(link.last().bytes.find("\"cmd\":\"status\"") != std::string::npos);
    CHECK(cs.link_drops() == 1);

    // And a second pass does not send it again: a retry is not a stream.
    cs.tick(20);
    CHECK(link.sent.size() == 1);
}

TEST_CASE("comms: a push that will never fit is counted once and not retried forever") {
    platform::host::Link link;
    link.declare_payload_bytes(hal::kMinimumLinkPayload);
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);
    cs.on_link_up(messages::LinkUp{1, link.payload_bytes()});

    cs.set_battery_state(full_battery(), power::PowerLevel::Normal);
    CHECK(cs.link_drops() == 1);
    for (uint32_t t = 10; t <= 100; t += 10) cs.tick(t);
    // Nothing about a frame too big changes by waiting, so waiting is not the
    // answer: one count, and the phone can ask for a status whenever it likes.
    CHECK(cs.link_drops() == 1);
    CHECK(link.sent.empty());
}
