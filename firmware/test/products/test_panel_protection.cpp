// What the panel's limits do to the refresh policy: held hot, washed cold, stopped, parked.
#include "test/support/screen_rig.h"

// Glass rated 0..50 C; the Go's soak case is 72.4 C, project/research/enclosure-and-mount-go.md.
TEST_CASE("screen policy: a panel too hot to drive is left unrefreshed, with its rails down") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    const int before = rig.chip.present_count;

    rig.die_temperature(go::ScreenService::kHoldAboveDeciCelsius + 1);
    rig.churn(t, 20);
    CHECK(rig.chip.present_count == before);
    CHECK_FALSE(rig.chip.rails_on);
}

// skyBlip stays on for the whole flight, so nothing but the pilot's switch may sleep the panel.
TEST_CASE("screen policy: a screen nobody changes is left awake, unrefreshed, rails down") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    const int before = rig.chip.present_count;
    const int sleeps = rig.chip.deep_sleeps;

    rig.run_seconds(t, 600);
    CHECK(rig.chip.present_count == before);
    CHECK(rig.chip.deep_sleeps == sleeps);
    CHECK(rig.chip.powered);
    CHECK_FALSE(rig.chip.rails_on);
}

// The ink migrates under bias, so a busy sky must not hold the rails up for a whole flight.
TEST_CASE("screen policy: a screen changing every second still rests between refreshes") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    rig.alarm(2);
    rig.churn(t, 120);
    rig.run_seconds(t, 2);

    CHECK(rig.chip.present_count > 100);
    CHECK_FALSE(rig.chip.rails_on);
    CHECK(rig.chip.deep_sleeps == 0);
}

TEST_CASE("screen policy: a held panel is not washed either, so nothing refreshes it hot") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    rig.churn(t, 2);
    rig.run_seconds(t, 1);
    const int before = rig.chip.present_count;

    rig.die_temperature(go::ScreenService::kHoldAboveDeciCelsius + 1);
    rig.run_seconds(t, 600);
    CHECK(rig.chip.present_count == before);
    CHECK_FALSE(rig.chip.rails_on);
}

TEST_CASE("screen policy: a supply warning is not washed, no refresh starts on a dying rail") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    rig.churn(t, 2);
    const int before = rig.chip.present_count;

    rig.state.supply_warned = true;
    rig.run_seconds(t, 600);
    CHECK(rig.chip.present_count == before);
    CHECK_FALSE(rig.chip.rails_on);
}

TEST_CASE("screen policy: the hold threshold is the rated limit, not a margin somebody chose") {
    CHECK(go::ScreenService::kHoldAboveDeciCelsius == 500);
    CHECK(go::ScreenService::kFullOnlyBelowDeciCelsius == 0);
}

// The rated limit is a temperature the panel works at, not the first one it refuses.
TEST_CASE("screen policy: the panel still refreshes at exactly its rated limit") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);

    rig.die_temperature(go::ScreenService::kHoldAboveDeciCelsius);
    const int before = rig.chip.present_count;
    rig.churn(t, 3);
    CHECK(rig.chip.present_count > before);

    rig.die_temperature(go::ScreenService::kHoldAboveDeciCelsius + 1);
    const int held = rig.chip.present_count;
    rig.churn(t, 3);
    CHECK(rig.chip.present_count == held);
}

TEST_CASE("screen policy: freezing point itself is warm enough for the fast waveform") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);

    rig.die_temperature(go::ScreenService::kFullOnlyBelowDeciCelsius);
    rig.churn(t, 2);
    CHECK_FALSE(rig.chip.last_full);

    rig.die_temperature(go::ScreenService::kFullOnlyBelowDeciCelsius - 1);
    rig.churn(t, 1);
    rig.run_seconds(t, 2);
    CHECK(rig.chip.last_full);
}

// A refresh already in flight when the panel goes out of range is left to finish.
TEST_CASE("screen policy: going hot mid-refresh does not abandon the frame on the glass") {
    Rig rig;
    uint32_t t = 0;
    rig.screen.tick(t += 1000);
    REQUIRE(rig.chip.present_count == 1);

    rig.die_temperature(go::ScreenService::kHoldAboveDeciCelsius + 1);
    rig.screen.tick(t += 100);
    rig.screen.tick(t += 2000);
    CHECK(rig.chip.present_count == 1);
    CHECK_FALSE(rig.chip.rails_on);
}

TEST_CASE("screen policy: the traffic picture comes back once the panel has cooled") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    rig.die_temperature(go::ScreenService::kHoldAboveDeciCelsius + 1);
    rig.churn(t, 5);
    const int held = rig.chip.present_count;

    rig.die_temperature(300);
    rig.churn(t, 3);
    CHECK(rig.chip.present_count > held);
}

// The partial LUT is the one that ghosts cold, and a blank traffic display is its own failure.
TEST_CASE("screen policy: below freezing every refresh is the compensated full one") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);

    rig.die_temperature(-50);
    for (int i = 0; i < 6; i++) {
        rig.churn(t, 1);
        rig.run_seconds(t, 2);
        CHECK(rig.chip.last_full);
    }
    CHECK(rig.screen.fasts_since_full() == 0);
}

// Forcing the full waveform must not also make cold a reason to refresh at all.
TEST_CASE("screen policy: a cold panel with a static frame is still never re-presented") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    const int before = rig.chip.present_count;

    rig.die_temperature(-100);
    rig.run_seconds(t, 60);
    CHECK(rig.chip.present_count == before);
}

TEST_CASE("screen policy: a cold panel is washed even mid-alarm, where a warm one is not") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    rig.alarm(3);

    rig.churn(t, 2);
    REQUIRE_FALSE(rig.chip.last_full);

    rig.die_temperature(-1);
    rig.churn(t, 1);
    rig.run_seconds(t, 2);
    CHECK(rig.chip.last_full);
}

TEST_CASE("screen policy: a board whose die sensor never read refreshes as it always did") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    REQUIRE_FALSE(rig.state.die_temperature_valid);

    rig.churn(t, 3);
    CHECK_FALSE(rig.chip.last_full);
    CHECK(rig.chip.powered);
}

TEST_CASE("screen policy: nothing routine is refreshed once the cell is at its cutoff") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    const int before = rig.chip.present_count;

    rig.state.power_level = power::PowerLevel::Cutoff;
    rig.churn(t, 10);
    CHECK(rig.chip.present_count == before);
    CHECK_FALSE(rig.chip.rails_on);
}

TEST_CASE("screen policy: a low cell still gets its traffic picture") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    const int before = rig.chip.present_count;

    rig.state.power_level = power::PowerLevel::Low;
    rig.churn(t, 3);
    CHECK(rig.chip.present_count > before);
}

TEST_CASE("screen policy: the white field the glass wears while off is drawn at the cutoff") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    const int before = rig.chip.present_count;

    rig.state.power_level = power::PowerLevel::Cutoff;
    rig.screen.set_power(false);
    CHECK(rig.chip.present_count == before + 1);
    CHECK(rig.chip.last_full);
    CHECK_FALSE(rig.chip.powered);
}

// POFCON fires milliseconds before the brownout reset, so the park frame cannot finish.
TEST_CASE("screen policy: a supply warning parks the panel without a park frame") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    const int before = rig.chip.present_count;

    rig.state.supply_warned = true;
    rig.screen.set_power(false);
    CHECK(rig.chip.present_count == before);
    CHECK_FALSE(rig.chip.powered);
}

TEST_CASE("screen policy: a supply warning stops the routine refreshes too") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    const int before = rig.chip.present_count;

    rig.state.supply_warned = true;
    rig.churn(t, 10);
    CHECK(rig.chip.present_count == before);
    CHECK_FALSE(rig.chip.rails_on);
}

// A held panel presents nothing, so nothing a pilot has not read reaches the glass.
TEST_CASE("screen policy: a held panel puts no new frame on the glass, dirty or not") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);

    rig.die_temperature(go::ScreenService::kHoldAboveDeciCelsius + 1);
    rig.run_seconds(t, 5);
    const int held = rig.chip.present_count;

    rig.screen.mark_dirty();
    rig.screen.next_page();
    rig.run_seconds(t, 5);
    CHECK(rig.chip.present_count == held);
}
