// Raw RSSI ranks distances, not radios. These pin down the arithmetic that takes
// the range back out of a signal, so two aircraft at the same distance with 12 dB
// between them is read as an antenna finding rather than as traffic. What must not
// happen is a verdict where there is no evidence: a relayed position carries the
// relay's radio, not the aircraft's, and an emitter without a position cannot be
// ranged at all.
#include "core/traffic/link.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::traffic;

namespace {

// 1e-7 degrees of latitude per metre north, the same 11132 m/deg the position
// maths uses, so a test distance and the code's distance cannot drift apart.
int32_t lat_offset_for(int32_t north_m) {
    return static_cast<int32_t>((static_cast<int64_t>(north_m) * 1000000) / 11132);
}

messages::OwnState own_at_equator() {
    messages::OwnState o{};
    o.fix_valid = true;
    o.lat_1e7 = 0;
    o.lon_1e7 = 0;
    o.alt_m = 1000;
    return o;
}

messages::AircraftObs emitter(int32_t north_m, int8_t rssi, messages::Source src) {
    messages::AircraftObs t{};
    t.valid_pos = true;
    t.addr = 0xABCD;
    t.lat_1e7 = lat_offset_for(north_m);
    t.lon_1e7 = 0;
    t.alt_m = 1000;
    t.rssi_dbm = rssi;
    t.source = src;
    return t;
}

}  // namespace

TEST_CASE("link: free-space loss hits the 868 MHz reference points") {
    // 32.4 + 20log10(f_MHz) + 20log10(d_km): 91.2 dB at 1 km, +20 dB per decade.
    CHECK(free_space_loss_db(100) == 71);
    CHECK(free_space_loss_db(1000) == 91);
    CHECK(free_space_loss_db(10000) == 111);
    CHECK(free_space_loss_db(100000) == 131);
    // Doubling the range costs 6 dB, wherever you start.
    CHECK(free_space_loss_db(4000) - free_space_loss_db(2000) == 6);
}

TEST_CASE("link: implied e.r.p. recovers the transmitter behind the signal") {
    // A 25 mW (14 dBm) emitter at 1 km arrives 91 dB down.
    const messages::OwnState own = own_at_equator();
    LinkRow row;
    REQUIRE(estimate_link(own, emitter(1000, -77, messages::Source::AdslDirect), row));
    CHECK(row.modelled);
    CHECK(row.slant_m == doctest::Approx(1000).epsilon(0.01));
    CHECK(row.implied_erp_dbm == doctest::Approx(14).epsilon(0.1));

    // Same aircraft twice as far and 6 dB weaker is the same radio: the index
    // is what stays put while RSSI moves.
    LinkRow far;
    REQUIRE(estimate_link(own, emitter(2000, -83, messages::Source::AdslDirect), far));
    CHECK(far.implied_erp_dbm == row.implied_erp_dbm);

    // And one that is 12 dB down at the same range reads 12 dB down.
    LinkRow weak;
    REQUIRE(estimate_link(own, emitter(1000, -89, messages::Source::AdslDirect), weak));
    CHECK(row.implied_erp_dbm - weak.implied_erp_dbm == 12);
}

TEST_CASE("link: a relayed position says nothing about the aircraft's radio") {
    // The RSSI on an uplink frame is the ground station's path, not the
    // aircraft's, so the row exists and the verdict does not.
    LinkRow row;
    REQUIRE(estimate_link(own_at_equator(), emitter(1000, -77, messages::Source::AdslUplink), row));
    CHECK(row.slant_m == doctest::Approx(1000).epsilon(0.01));
    CHECK_FALSE(row.modelled);
    CHECK(row.implied_erp_dbm == 0);
}

TEST_CASE("link: ALP-TAS traffic is measured on the same scale as ADS-L") {
    LinkRow row;
    REQUIRE(estimate_link(own_at_equator(), emitter(1000, -77, messages::Source::Alptas), row));
    CHECK(row.modelled);
    CHECK(row.implied_erp_dbm == doctest::Approx(14).epsilon(0.1));
}

TEST_CASE("link: too close to model, still worth listing") {
    LinkRow row;
    REQUIRE(estimate_link(own_at_equator(), emitter(30, -30, messages::Source::AdslDirect), row));
    CHECK(row.slant_m < kMinModelledRangeM);
    CHECK_FALSE(row.modelled);
}

TEST_CASE("link: an emitter without a position cannot be ranged") {
    messages::AircraftObs t = emitter(1000, -77, messages::Source::AdslDirect);
    t.valid_pos = false;
    LinkRow row;
    CHECK_FALSE(estimate_link(own_at_equator(), t, row));

    messages::OwnState blind = own_at_equator();
    blind.fix_valid = false;
    CHECK_FALSE(estimate_link(blind, emitter(1000, -77, messages::Source::AdslDirect), row));
}

TEST_CASE("link: vertical separation counts as range") {
    // Directly overhead by 2 km is a 2 km path, not a zero one.
    messages::AircraftObs t = emitter(0, -77, messages::Source::AdslDirect);
    t.alt_m = 3000;
    LinkRow row;
    REQUIRE(estimate_link(own_at_equator(), t, row));
    CHECK(row.up_m == 2000);
    CHECK(row.slant_m == doctest::Approx(2000).epsilon(0.01));
}

TEST_CASE("link: the wave takes the hypotenuse, not the ground track") {
    // 3 km out and 4 km up is a 5 km path. Charging the emitter only for its
    // horizontal separation would credit it with 4 dB it never radiated.
    messages::AircraftObs t = emitter(3000, -91, messages::Source::AdslDirect);
    t.alt_m = 5000;
    LinkRow row;
    REQUIRE(estimate_link(own_at_equator(), t, row));
    CHECK(row.slant_m == doctest::Approx(5000).epsilon(0.01));
    CHECK(row.implied_erp_dbm == free_space_loss_db(5000) - 91);
    CHECK(row.implied_erp_dbm != free_space_loss_db(3000) - 91);
}

TEST_CASE("link: ranking puts the nearest emitters first and drops the rest") {
    TrafficTable table;
    const int32_t ranges[6] = {8000, 1000, 5000, 300, 12000, 2500};
    for (int i = 0; i < 6; i++) {
        messages::AircraftObs t = emitter(ranges[i], -90, messages::Source::AdslDirect);
        t.addr = 0x100u + static_cast<uint32_t>(i);
        table.update(t, 100);
    }

    LinkRow rows[4];
    const int n = rank_by_range(table, own_at_equator(), rows, 4);
    REQUIRE(n == 4);
    CHECK(rows[0].addr == 0x103);  // 300 m
    CHECK(rows[1].addr == 0x101);  // 1000 m
    CHECK(rows[2].addr == 0x105);  // 2500 m
    CHECK(rows[3].addr == 0x102);  // 5000 m
    for (int i = 1; i < n; i++) CHECK(rows[i - 1].slant_m <= rows[i].slant_m);
}

TEST_CASE("link: ranking skips what it cannot range") {
    TrafficTable table;
    messages::AircraftObs positioned = emitter(4000, -95, messages::Source::AdslDirect);
    positioned.addr = 0x200;
    table.update(positioned, 100);

    messages::AircraftObs blind = emitter(1000, -70, messages::Source::AdslDirect);
    blind.addr = 0x201;
    blind.valid_pos = false;
    table.update(blind, 100);

    LinkRow rows[4];
    const int n = rank_by_range(table, own_at_equator(), rows, 4);
    CHECK(n == 1);
    CHECK(rows[0].addr == 0x200);
}
