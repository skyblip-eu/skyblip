// What a pilot's tablet actually receives from a running skyBlip Go.
//
// The tree formatted $PFLAA/$PFLAU/$PGRMZ for months, tested the formatters
// hard, routed the endpoint, advertised the characteristic - and never sent a
// byte, because no service called any of it. So every case here asserts on the
// bytes that left the link, reassembled the way an EFB reassembles them: the
// frames are a stream, and a sentence is what lies between two CRLFs.
//
// Interoperability, in one place, because getting it wrong is worse than
// shipping nothing:
//   $PFLAA - the traffic sentence every app reads. SkyDemon, XCSoar and SDVFR
//            Next all draw from it; SkyDemon accepts only IDType 1 or 2 (see
//            oss/SoftRF-moshe-braner/.../libraries/OGN/ads-l.h:657).
//   $PFLAU - system state and nearest threat. XCSoar sounds its own alarms off
//            it (oss/SoftRF-moshe-braner/.../src/TrafficHelper.cpp:1913); the
//            open-source GATAS suppresses it for SkyDemon
//            (oss/openace/.../dataport.cpp:31). It is one sentence a second and
//            it is the one this service refuses to drop.
//   $PGRMZ - barometric altitude, read as PRESSURE altitude by the app, which
//            then applies its own QNH. Hence the datum asserted below.
#include <string>
#include <vector>

#include "core/flight/atmosphere.h"
#include "core/protocol/adsl.h"
#include "core/protocol/air.h"
#include "core/settings/settings.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/platform/host/clock.h"
#include "hardware/platform/host/link.h"
#include "runtime/null.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

// Every byte the device put on the NMEA endpoint, in order: an EFB sees one
// stream, not a sequence of notifications.
std::string stream(Rig& rig) {
    std::string all;
    for (const auto& frame : rig.platform.link().sent)
        if (frame.endpoint == messages::Endpoint::Nmea) all += frame.bytes;
    return all;
}

std::vector<std::string> sentences(Rig& rig) {
    std::vector<std::string> out;
    const std::string all = stream(rig);
    size_t at = 0;
    while (true) {
        const size_t end = all.find("\r\n", at);
        if (end == std::string::npos) break;
        out.push_back(all.substr(at, end - at));
        at = end + 2;
    }
    return out;
}

bool checksum_ok(const std::string& sentence) {
    const size_t star = sentence.find('*');
    if (star == std::string::npos || sentence.size() < star + 3) return false;
    uint8_t sum = 0;
    for (size_t i = 1; i < star; i++) sum ^= static_cast<uint8_t>(sentence[i]);
    const std::string hex = sentence.substr(star + 1, 2);
    return static_cast<uint8_t>(std::stoi(hex, nullptr, 16)) == sum;
}

std::vector<std::string> fields(const std::string& sentence) {
    std::vector<std::string> out;
    const std::string body = sentence.substr(0, sentence.find('*'));
    size_t at = 0;
    while (true) {
        const size_t comma = body.find(',', at);
        if (comma == std::string::npos) {
            out.push_back(body.substr(at));
            return out;
        }
        out.push_back(body.substr(at, comma - at));
        at = comma + 1;
    }
}

int count_of(Rig& rig, const char* kind) {
    int n = 0;
    for (const std::string& s : sentences(rig))
        if (s.rfind(kind, 0) == 0) n++;
    return n;
}

// A burst on air, from a transmitter that is where the case says it is. It goes
// in as chips through the same sync-window strip the SX1262 does, so the
// product's own decoder is what turns it into a target: nothing here writes to
// the traffic table.
void hear(Rig& rig, uint32_t addr, int32_t north_m, int32_t east_m, int32_t up_m,
          uint16_t track_c9 = 256) {
    messages::OwnState transmitter = rig.state().own;
    transmitter.lat_1e7 += static_cast<int32_t>(static_cast<int64_t>(north_m) * 1000000 / 11132);
    transmitter.lon_1e7 += static_cast<int32_t>(static_cast<int64_t>(east_m) * 1000000 / 7460);
    transmitter.alt_m += up_m;
    transmitter.track_c9 = track_c9;
    transmitter.speed_q = 160;

    protocol::AdslPacket packet;
    protocol::from_own(packet, transmitter, addr, /*addr_table=*/6, /*aircraft_cat=*/4,
                       /*stealth=*/false);
    packet.scramble();
    packet.set_crc();

    uint8_t chips[protocol::kTxChipBytes];
    const size_t chip_len = protocol::encode_mband(
        protocol::kAdslSyncWord, reinterpret_cast<const uint8_t*>(&packet.Version),
        protocol::kAdslFrameBytes, chips);

    messages::RfEvent event{};
    event.type = messages::RfEventType::RxDone;
    event.rssi_dbm = -80;
    event.len = models::Sx1262::deliver_after_sync(chips, static_cast<uint8_t>(chip_len),
                                                   protocol::kSharedSync, protocol::kSharedSyncBits,
                                                   event.data.data(), protocol::kRxChipBytes);
    rig.product.bus().rf.push(event);
}

// Airborne, timed and moving: everything below needs a fix, because a relative
// position has no meaning without one.
void fly(Rig& rig, uint32_t& t, uint32_t seconds) { rig.seconds(t, seconds, 100, 900); }

}  // namespace

// THE GUARD. Delete the service from the product's list, or its call to
// hal::Link::send, and this is the case that goes red. It is written the way a
// pilot experiences the feature: pair a tablet, see traffic; walk away, see it
// stop.
TEST_CASE("nmea: a tablet that pairs starts hearing sentences, and they stop when it leaves") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);

    // Nobody is listening, so nothing is said - and nothing was formatted to say.
    CHECK(rig.platform.link().count_on(messages::Endpoint::Nmea) == 0);

    rig.raise_link();
    fly(rig, t, 3);
    REQUIRE(rig.link_up());
    const std::vector<std::string> heard = sentences(rig);
    REQUIRE(heard.size() >= 3);
    for (const std::string& s : heard) CHECK(checksum_ok(s));
    // The status sentence is the heartbeat: one per second, with or without
    // traffic, which is how an app knows the device is alive and has a fix.
    CHECK(count_of(rig, "$PFLAU") >= 3);

    rig.platform.link().clear();
    rig.drop_link();
    fly(rig, t, 3);
    CHECK(rig.platform.link().count_on(messages::Endpoint::Nmea) == 0);
}

TEST_CASE("nmea: an aircraft heard over the air becomes a $PFLAA a tablet can parse") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);
    rig.raise_link();
    fly(rig, t, 1);
    rig.platform.link().clear();

    // 1200 m north, 800 m east, 150 m above.
    hear(rig, 0xC5D804, 1200, 800, 150);
    fly(rig, t, 2);

    std::string traffic;
    for (const std::string& s : sentences(rig))
        if (s.rfind("$PFLAA,", 0) == 0) traffic = s;
    REQUIRE_FALSE(traffic.empty());
    CHECK(checksum_ok(traffic));

    const std::vector<std::string> f = fields(traffic);
    REQUIRE(f.size() >= 12);
    CHECK(std::stoi(f[2]) > 900);  // relative north, metres
    CHECK(std::stoi(f[2]) < 1500);
    CHECK(std::stoi(f[3]) > 500);  // relative east
    CHECK(std::stoi(f[3]) < 1100);
    CHECK(std::stoi(f[4]) > 100);  // relative vertical, + is above
    // IDType 2 (the FLARM address table), which with 1 is all SkyDemon accepts,
    // then the 24-bit address as six hex digits.
    CHECK(f[5] == "2");
    CHECK(f[6] == "C5D804");
    CHECK(f[11] == "1");  // ALP-TAS aircraft type: glider
}

TEST_CASE("nmea: the level the device alarms on is the level that reaches $PFLAU") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);
    rig.raise_link();
    fly(rig, t, 1);

    // Co-altitude and inside the urgent ring, flying at us: the same geometry
    // core/traffic grades, not a level written into the table by hand.
    for (int pass = 0; pass < 4; pass++) {
        hear(rig, 0x112233, 300, 0, 10, /*track_c9=*/256);
        fly(rig, t, 1);
    }
    REQUIRE(rig.state().alarm_level >= 2);

    std::string status;
    for (const std::string& s : sentences(rig))
        if (s.rfind("$PFLAU,", 0) == 0) status = s;
    REQUIRE_FALSE(status.empty());
    const std::vector<std::string> f = fields(status);
    REQUIRE(f.size() >= 10);
    CHECK(std::stoi(f[1]) >= 1);  // targets heard
    CHECK(std::stoi(f[3]) == 2);  // 3D fix
    CHECK(std::stoi(f[5]) == static_cast<int>(rig.state().alarm_level));
    // Own-ship is tracking east and the threat is due north of it, so it is off
    // the left wing: the relative bearing is signed, half a turn either way, and
    // an app that drew 270 here would put the arrow on the wrong side.
    CHECK(std::stoi(f[6]) < -60);
    CHECK(std::stoi(f[6]) > -120);
    CHECK(std::stoi(f[7]) == 2);   // alarm type: aircraft
    CHECK(std::stoi(f[9]) < 600);  // relative distance, metres
    CHECK(f[10] == "112233");      // the threat's id
}

TEST_CASE(
    "nmea: every frame fits the payload the central negotiated, down to what BLE guarantees") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    // The phone that never exchanges an MTU: 23 bytes of ATT, 20 of payload.
    // A $PFLAA does not fit in one of those, so a sender that framed one
    // sentence per notification would send this pilot nothing at all.
    rig.platform.link().declare_payload_bytes(hal::kMinimumLinkPayload);
    fly(rig, t, 3);
    rig.raise_link();
    fly(rig, t, 1);
    hear(rig, 0xABCDEF, 900, -400, -60);
    fly(rig, t, 2);

    int frames = 0;
    for (const auto& frame : rig.platform.link().sent) {
        if (frame.endpoint != messages::Endpoint::Nmea) continue;
        frames++;
        CHECK(frame.bytes.size() <= hal::kMinimumLinkPayload);
    }
    CHECK(frames > 0);
    // The controller's refusal never happened: nothing oversized was offered.
    CHECK(rig.platform.link().refused_oversize == 0);
    // And the stream still reassembles into whole, checksummed sentences.
    const std::vector<std::string> heard = sentences(rig);
    REQUIRE(heard.size() >= 2);
    for (const std::string& s : heard) CHECK(checksum_ok(s));
    bool saw_traffic = false;
    for (const std::string& s : heard) saw_traffic = saw_traffic || s.rfind("$PFLAA,", 0) == 0;
    CHECK(saw_traffic);
}

TEST_CASE("nmea: more targets than one pass carries are all refreshed inside the bound") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);
    rig.raise_link();
    fly(rig, t, 1);

    // A full table: five times what one pass may carry, so the bound below is
    // exercised at the worst case the device can be in rather than near it.
    const int count = traffic::TrafficTable::kCapacity;
    for (int i = 0; i < count; i++) {
        hear(rig, 0x200000u + static_cast<uint32_t>(i), 500 + 100 * i, 200 - 30 * i, 40 + 5 * i);
        // The radio queue holds eight events, and the traffic service drains it
        // once a pass: a sky this busy arrives over several passes, as it does
        // on air.
        if ((i + 1) % 4 == 0) {
            rig.run(t, t + 40);
            t += 50;
        }
    }
    fly(rig, t, 1);
    REQUIRE(rig.state().traffic.count() == count);
    rig.platform.link().clear();

    // One bound's worth of passes, and every aircraft in the table has been
    // named at least once inside it.
    fly(rig, t, go::NmeaService::kTargetRefreshBoundMs / 1000);
    const std::vector<std::string> heard = sentences(rig);
    for (int i = 0; i < count; i++) {
        char id[8];
        std::snprintf(id, sizeof(id), "%06X", 0x200000u + static_cast<uint32_t>(i));
        bool named = false;
        for (const std::string& s : heard)
            named = named || (s.rfind("$PFLAA,", 0) == 0 && s.find(id) != std::string::npos);
        CHECK_MESSAGE(named, "target ", id, " was never refreshed inside the bound");
    }
    // No pass spends its whole budget on traffic and drops the alarm sentence.
    CHECK(count_of(rig, "$PFLAU") >=
          static_cast<int>(go::NmeaService::kTargetRefreshBoundMs / 1000));
    // ...and no pass sends more traffic than the cap the bound is derived from.
    CHECK(count_of(rig, "$PFLAA") <=
          go::NmeaService::kTargetsPerPass *
              (static_cast<int>(go::NmeaService::kTargetRefreshBoundMs / 1000) + 1));
}

TEST_CASE(
    "nmea: $PGRMZ carries pressure altitude on the standard datum, not the pilot's subscale") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    // The air the aircraft is actually flying through, and a subscale nowhere
    // near standard, set on the device the way the settings page sets it.
    rig.platform.baro().chip.set_pressure_pa(90000);
    rig.state().qnh_pa = 98000;
    fly(rig, t, 3);
    rig.raise_link();
    fly(rig, t, 2);
    REQUIRE(rig.state().baro_active);

    std::string altitude;
    for (const std::string& s : sentences(rig))
        if (s.rfind("$PGRMZ,", 0) == 0) altitude = s;
    REQUIRE_FALSE(altitude.empty());
    CHECK(checksum_ok(altitude));
    const std::vector<std::string> f = fields(altitude);
    REQUIRE(f.size() >= 3);
    CHECK(f[2] == "F");

    const int32_t standard_cm = flight::pressure_to_alt_cm(rig.state().pressure_pa);
    const int32_t on_subscale_cm =
        flight::alt_cm_on_setting(rig.state().pressure_pa, rig.state().qnh_pa);
    const int32_t sent_cm = static_cast<int32_t>(std::stoi(f[1])) * 3048 / 100;
    // What an EFB does with this figure is apply its own QNH, so the figure has
    // to be the datum-free one: pressure altitude on 1013.25. Within a foot of
    // the standard altitude, and hundreds of metres from the altitude the panel
    // shows the pilot on the subscale he set.
    CHECK(standard_cm > 90000);
    CHECK(std::abs(sent_cm - standard_cm) < 40);
    CHECK(std::abs(standard_cm - on_subscale_cm) > 20000);
}

namespace {

// The service alone, with a link that is up and a fix that is valid, so that the
// only thing a case changes is what the product claims to be.
struct FeatureRig {
    platform::host::Clock clock;
    platform::host::Link link;
    runtime::NullRoles null;
    hal::Roles roles{clock,          null.rf,          link,    null.display, null.kv,
                     null.log_flash, null.annunciator, null.dfu};
    bus::Bus bus{};
    bus::State state{};
    runtime::Context context{roles, bus, state};
    settings::Settings settings{};
    comms::ConfigService config{link, settings};
    go::NmeaService nmea;

    explicit FeatureRig(go::Feature declared) : nmea(context, declared) {
        roles.capabilities = hal::Capability::Link;
        state.own.fix_valid = true;
        state.own.utc_valid = true;
        state.own.lat_1e7 = 485000000;
        state.own.lon_1e7 = 85000000;
        config.on_link_up(messages::LinkUp{1, platform::host::Link::kDefaultPayloadBytes});
        nmea.attach_config(config);
    }

    int frames() { return link.count_on(messages::Endpoint::Nmea); }
};

}  // namespace

TEST_CASE("nmea: a product that does not declare the companion link says nothing on it") {
    FeatureRig silent(go::Feature::AdslRx);
    for (uint32_t t = 0; t <= 5000; t += 100) silent.nmea.tick(t);
    CHECK_FALSE(silent.nmea.enabled());
    CHECK(silent.frames() == 0);

    // The same rig, the same link, the same fix: the feature is the difference,
    // which is what makes it a gate and not decoration.
    FeatureRig speaking(go::Feature::CompanionLink);
    for (uint32_t t = 0; t <= 5000; t += 100) speaking.nmea.tick(t);
    CHECK(speaking.nmea.enabled());
    CHECK(speaking.frames() > 0);
}
