// The e-paper refresh policy, tested through the screen service against the
// real SSD1681 driver and its model. The policy under test: refresh only when
// the pixels changed, fast (flicker-free) by default, and schedule the flashing
// full refresh around the traffic picture: never during an alarm, opportunistically
// once the sky has been quiet, forced only at the hard ceiling.
#include "doctest/doctest.h"
#include "hardware/parts/ssd1681/model.h"
#include "hardware/parts/ssd1681/ssd1681.h"
#include "hardware/platform/host/clock.h"
#include "products/skyblip_go/services/screen.h"
#include "runtime/null.h"

using namespace skyblip;

namespace {

// The screen service alone: state is written by hand, so alarm level and
// traffic are exactly what the case says, with no alarm service re-deriving
// them.
struct Rig {
    models::Ssd1681 chip;
    parts::Ssd1681 epd{chip, chip, chip.dc, chip.rst, chip.busy};
    platform::host::Clock clock;
    runtime::NullRoles null;
    hal::Roles roles{clock,   null.rf,        null.link,        epd,  // epd fills Display
                     null.kv, null.log_flash, null.annunciator, null.dfu};
    bus::Bus bus{};
    bus::State state{};
    runtime::Context context{roles, bus, state};
    go::ScreenService screen{context};

    Rig() {
        roles.capabilities = hal::Capability::Display;
        // With a fix the radar page draws rings and the range label, so churn()
        // below produces real pixel changes.
        state.own.fix_valid = true;
        epd.begin();
    }

    // One service tick per second, the render cadence.
    void run_seconds(uint32_t& t, int seconds) {
        for (int i = 0; i < seconds; i++) {
            t += 1000;
            screen.tick(t);
        }
    }

    // Forces a visible change every second: the coverage indicator character
    // on the radar page flips. Orthogonal to alarm level and traffic count, so
    // the quiet-sky logic stays in the case's hands.
    void churn(uint32_t& t, int seconds) {
        for (int i = 0; i < seconds; i++) {
            state.clock.utc_valid = !state.clock.utc_valid;
            run_seconds(t, 1);
        }
    }

    void alarm(uint8_t level) { state.alarm_level = level; }
};

}  // namespace

TEST_CASE("screen policy: a static frame is never re-presented") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    CHECK(rig.chip.present_count == 1);  // the boot frame, full
    CHECK(rig.chip.last_full);

    rig.run_seconds(t, 30);  // nothing on screen changes
    CHECK(rig.chip.present_count == 1);
}

TEST_CASE("screen policy: changing frames refresh fast until the full is owed") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);  // boot full

    rig.churn(t, go::ScreenService::kFastPerFull);
    CHECK(rig.chip.present_count > 1);
    CHECK_FALSE(rig.chip.last_full);

    // Debt reached kFastPerFull and the sky is empty: the next change pays it.
    rig.churn(t, 1);
    CHECK(rig.chip.last_full);
    rig.churn(t, 1);
    rig.run_seconds(t, 1);            // the full is still settling when the change arrives
    CHECK_FALSE(rig.chip.last_full);  // and the counter restarted
    CHECK(rig.screen.fasts_since_full() == 1);
}

TEST_CASE("screen policy: no full refresh lands while an alarm is active") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);  // boot full

    rig.alarm(2);
    int fulls = 0;
    for (int i = 0; i < go::ScreenService::kFastHardCeiling - 1; i++) {
        rig.churn(t, 1);
        if (rig.chip.last_full) fulls++;
    }
    // Far past kFastPerFull, still not one flash in the pilot's face.
    CHECK(rig.screen.fasts_since_full() > go::ScreenService::kFastPerFull);
    CHECK(fulls == 0);
}

TEST_CASE("screen policy: the hard ceiling forces the full even mid-alarm") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);

    rig.alarm(3);
    rig.churn(t, go::ScreenService::kFastHardCeiling + 3);
    CHECK(rig.screen.fasts_since_full() < go::ScreenService::kFastHardCeiling);
}

TEST_CASE("screen policy: an owed full is paid unprovoked once the sky stays empty") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);

    // A short alarm leaves ghost debt behind.
    rig.alarm(2);
    rig.churn(t, 5);
    CHECK(rig.screen.fasts_since_full() >= 5);
    rig.alarm(0);

    // The screen goes static, so nothing would present on its own. After
    // kSkyEmptyBeforeFullMs of quiet the wash lands anyway.
    const int before = rig.chip.present_count;
    rig.run_seconds(t, go::ScreenService::kSkyEmptyBeforeFullMs / 1000 + 2);
    CHECK(rig.chip.present_count == before + 1);
    CHECK(rig.chip.last_full);
    CHECK(rig.screen.fasts_since_full() == 0);
}

TEST_CASE("screen policy: a page change prefers a full refresh, but not during an alarm") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    rig.churn(t, 2);  // some fasts on the counter
    CHECK_FALSE(rig.chip.last_full);

    rig.screen.next_page();
    rig.run_seconds(t, 2);
    CHECK(rig.chip.last_full);  // layout swap: worst ghosting case, washed

    rig.alarm(2);
    rig.screen.next_page();
    rig.run_seconds(t, 2);
    CHECK_FALSE(rig.chip.last_full);  // same swap under alarm: stays fast
}

// The settings page is the one page that keeps the button to itself, so it is
// the one page that has to be able to give it back without being asked.
TEST_CASE("screen policy: converging traffic takes the settings page back off the glass") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    for (int i = 0; i < 4; i++) rig.screen.next_page();
    REQUIRE(rig.screen.page() == go::Page::Settings);
    rig.run_seconds(t, 2);
    REQUIRE(rig.screen.editor().active());

    // An advisory is not worth taking a pilot's page away.
    rig.alarm(1);
    rig.run_seconds(t, 2);
    CHECK(rig.screen.page() == go::Page::Settings);

    // A bearing worth turning the head for is. The menu goes, the traffic
    // picture comes back, and no wash flashes while the alarm stands.
    rig.alarm(go::ScreenService::kAlarmTakesGlass);
    rig.run_seconds(t, 2);
    CHECK(rig.screen.page() == go::Page::Radar);
    CHECK_FALSE(rig.screen.editor().active());
    CHECK_FALSE(rig.chip.last_full);
}

TEST_CASE("screen policy: presents wait for the panel, none is issued mid-refresh") {
    Rig rig;
    uint32_t t = 0;
    rig.screen.tick(t += 1000);  // boot full: panel busy ~1.5 s
    const int count = rig.chip.present_count;

    rig.state.clock.utc_valid = true;  // a visible change, right away
    rig.screen.mark_dirty();
    rig.screen.tick(t += 100);  // 1.1 s: full not settled yet
    CHECK(rig.chip.present_count == count);

    rig.screen.tick(t += 1500);  // settled: the pending change lands
    CHECK(rig.chip.present_count == count + 1);
}
