// End-to-end host scenario (no hardware): the full skyBlip Go air-side pipeline.
//   GNSS NMEA -> OwnState
//   ADS-L direct RX (scramble+crc+manchester over a BER channel) -> obs -> table
//   alarm assessment -> $PFLAA/$PFLAU to the EFB link
//   ADS-L uplink RX -> obs -> table dedup/merge
// This is the "run all of core/ headless" harness and doubles as
// the composition sanity check that everything fits together with no #ifdef.
#include <cstring>
#include <string>

#include "core/fec/manchester.h"
#include "core/gnss/nmea.h"
#include "core/protocol/adsl.h"
#include "core/protocol/adsl_uplink.h"
#include "core/protocol/nmea_out.h"
#include "core/traffic/alarm.h"
#include "core/traffic/table.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/link.h"
#include "test/support/rf_channel.h"

using namespace skyblip;

namespace {
messages::OwnState own_from_gnss(const char* rmc, const char* gga) {
    gnss::NmeaParser p;
    p.parse_line(rmc, static_cast<int>(std::strlen(rmc)));
    p.parse_line(gga, static_cast<int>(std::strlen(gga)));
    const gnss::GnssFix& f = p.fix();
    messages::OwnState o{};
    o.fix_valid = f.valid;
    o.utc_valid = f.utc_valid;
    o.pps_locked = true;
    o.lat_1e7 = f.lat_1e7;
    o.lon_1e7 = f.lon_1e7;
    o.alt_m = f.alt_m;
    o.speed_q = f.speed_q;
    o.track_c9 = f.track_c9;
    o.utc = f.utc;
    o.sats = f.sats;
    o.flight_state = 2;
    return o;
}
}  // namespace

TEST_CASE("scenario: GNSS -> own, direct ADS-L RX over BER channel -> alarm -> NMEA") {
    // 1) own-ship from GNSS
    messages::OwnState own =
        own_from_gnss("$GPRMC,120000,A,4807.000,N,00800.000,E,050.0,000.0,230324,,*1C",
                      "$GPGGA,120000,4807.000,N,00800.000,E,1,09,0.8,1000.0,M,47,M,,*6F");
    REQUIRE(own.fix_valid);

    // 2) an intruder ~800 m north, co-altitude, head-on. Own-ship is tracking
    //    north at 50 kt; the intruder tracks south at the same speed, so the two
    //    close at about 51 m/s and meet in under the urgent time to impact. It
    //    used to be a copy of own-ship displaced north, which is a chase at a
    //    constant 800 m and alarms for the range alone - the assertion below
    //    says which of the two this fixture means.
    messages::OwnState intruder_state = own;
    intruder_state.lat_1e7 = own.lat_1e7 + static_cast<int32_t>((int64_t)800 * 1000000 / 11132);
    intruder_state.track_c9 = 256;  // cordic9: 512 to the turn, so due south
    intruder_state.utc = own.utc;
    protocol::AdslPacket tx;
    protocol::from_own(tx, intruder_state, 0xC5D804, /*table=*/6, /*cat=*/4, /*stealth=*/false);
    tx.scramble();
    tx.set_crc();

    // 3) go over the air: manchester encode 24 data bytes, inject light BER,
    //    manchester decode, CRC-correct, verify, descramble.
    uint8_t coded[48];
    fec::manchester_encode(reinterpret_cast<const uint8_t*>(&tx.Version),
                           protocol::AdslPacket::kDataBytes, coded);
    models::RfChannel chan(12345);
    chan.apply_ber(coded, sizeof(coded), 0.002);  // ~0.2% chip errors

    protocol::AdslPacket rx = tx;  // start from a copy; overwrite the data region
    uint8_t err[protocol::AdslPacket::kDataBytes];
    fec::manchester_decode(coded, protocol::AdslPacket::kDataBytes,
                           reinterpret_cast<uint8_t*>(&rx.Version), err);
    rx.correct(err, 6);
    REQUIRE(rx.check_crc() == 0);  // recovered a valid packet
    rx.descramble();

    // 4) decode -> obs -> table
    messages::AircraftObs obs;
    protocol::to_obs(rx, own.utc, 500, -80, messages::Source::AdslDirect, obs);
    CHECK(obs.addr == 0xC5D804u);

    traffic::TrafficTable table;
    int idx = table.update(obs, own.utc);
    REQUIRE(idx >= 0);
    CHECK(table.count() == 1);

    // 5) alarm: the closure comes off the relative velocity vector, so a level 3
    //    here is the geometry saying so and not the range gate it used to be.
    traffic::AlarmAssessment a = traffic::assess(own, obs);
    CHECK(a.valid);
    CHECK(a.rel_dist_m > 700);
    CHECK(a.rel_dist_m < 900);
    CHECK(a.closing_mps > 40);
    CHECK(a.level == 3);

    // The same aircraft in the same place, flying the way we are: nothing is
    // arriving, and the proximity ring still draws it because 800 m abeam is
    // worth knowing about. That is a level 2, and it is not an alarm.
    messages::AircraftObs chase = obs;
    chase.track_c9 = own.track_c9;
    const traffic::AlarmAssessment following = traffic::assess(own, chase);
    CHECK(following.closing_mps < traffic::kClosingFloorMps);
    CHECK(following.level == 2);

    table.at(idx)->alarm_level = a.level;

    // 6) NMEA out to the EFB link
    platform::host::Link efb;
    char buf[128];
    int n = protocol::format_pflaa(buf, sizeof(buf), own, obs, a.level);
    REQUIRE(n > 0);
    efb.send(messages::Endpoint::Nmea, ConstByteSpan(reinterpret_cast<uint8_t*>(buf), n));
    n = protocol::format_pflau(buf, sizeof(buf), own, table.count(), &obs, a.level,
                               a.rel_bearing_deg, a.rel_vert_m, a.rel_dist_m);
    efb.send(messages::Endpoint::Nmea, ConstByteSpan(reinterpret_cast<uint8_t*>(buf), n));
    CHECK(efb.count_on(messages::Endpoint::Nmea) == 2);
    CHECK(efb.sent[0].bytes.find("C5D804") != std::string::npos);
}

TEST_CASE("scenario: uplink RX merges with direct RX (dedup, prefer direct)") {
    messages::OwnState own =
        own_from_gnss("$GPRMC,120000,A,4807.000,N,00800.000,E,050.0,000.0,230324,,*1C",
                      "$GPGGA,120000,4807.000,N,00800.000,E,1,09,0.8,1000.0,M,47,M,,*6F");

    traffic::TrafficTable table;

    // First seen only via uplink (relayed from a ground station).
    messages::AircraftObs up{};
    up.addr = 0x3FBEEF;
    up.addr_table = 5;  // ICAO (from ADS-B via uplink)
    up.valid_pos = true;
    up.lat_1e7 = own.lat_1e7 + 50000;
    up.lon_1e7 = own.lon_1e7;
    up.alt_m = 1100;
    up.source = messages::Source::AdslUplink;
    up.rx_utc = own.utc;

    // Encode+decode through the real uplink codec (the anti-drift lock).
    protocol::AdslUplink codec;
    uint8_t frame[protocol::AdslUplink::kFrameBytes];
    REQUIRE(codec.encode(&up, 1, 0, frame) == Status::Ok);
    messages::AircraftObs decoded[4];
    protocol::AdslUplink::DecodeStats st;
    REQUIRE(codec.decode(frame, decoded, 4, st) == Status::Ok);
    REQUIRE(st.targets == 1);
    table.update(decoded[0], own.utc);
    CHECK(table.at(table.find(5, 0x3FBEEF))->obs.source == messages::Source::AdslUplink);

    // Later the same aircraft is heard directly on M-band -> prefer direct.
    messages::AircraftObs direct = decoded[0];
    direct.source = messages::Source::AdslDirect;
    direct.rx_utc = own.utc + 1;
    table.update(direct, own.utc + 1);
    CHECK(table.count() == 1);  // merged, not duplicated
    CHECK(table.at(table.find(5, 0x3FBEEF))->obs.source == messages::Source::AdslDirect);
}
