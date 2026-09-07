// What the power service publishes about temperature, and how long a reading stands.
#include "doctest/doctest.h"
#include "hardware/platform/host/clock.h"
#include "products/skyblip_go/services/power.h"
#include "runtime/null.h"

using namespace skyblip;

namespace {

struct Sensor : hal::DieTemperature {
    int16_t decicelsius{250};
    bool answers{true};
    uint32_t reads{0};

    bool read(int16_t& out) override {
        reads++;
        if (!answers) return false;
        out = decicelsius;
        return true;
    }
};

struct Rig {
    platform::host::Clock clock;
    runtime::NullRoles null;
    hal::Roles roles{clock,   null.rf,        null.link,        null.display,
                     null.kv, null.log_flash, null.annunciator, null.dfu};
    bus::Bus bus{};
    bus::State state{};
    runtime::Context context{roles, bus, state};
    go::PowerService power{context};
    Sensor sensor{};

    Rig() {
        roles.capabilities = hal::Capability::DieTemperature;
        power.attach_die_temperature(sensor);
    }
};

constexpr uint32_t kPeriod = go::PowerService::kDieStaleMs / 3;

}  // namespace

TEST_CASE("thermal: the freshness window is spelled out in seconds, not left to drift") {
    CHECK(go::PowerService::kDieStaleMs == 30000);
}

TEST_CASE("thermal: a reading the sensor gave is published where the panel gate reads it") {
    Rig rig;
    rig.sensor.decicelsius = 421;
    rig.power.tick(1000);

    CHECK(rig.state.die_temperature_valid);
    CHECK(rig.state.die_decicelsius == 421);
}

TEST_CASE("thermal: a board with no sensor publishes no reading, never a zero") {
    Rig rig;
    rig.roles.capabilities = hal::Capabilities{};
    rig.power.tick(1000);

    CHECK_FALSE(rig.state.die_temperature_valid);
    CHECK(rig.sensor.reads == 0);
}

// The gate would otherwise hold a hot panel for ever on a reading nobody refreshed.
TEST_CASE("thermal: a sensor that stops answering stops gating the panel") {
    Rig rig;
    rig.sensor.decicelsius = 700;
    uint32_t t = 1000;
    rig.power.tick(t);
    REQUIRE(rig.state.die_temperature_valid);

    rig.sensor.answers = false;
    for (uint32_t elapsed = kPeriod; elapsed <= go::PowerService::kDieStaleMs; elapsed += kPeriod) {
        rig.power.tick(t + elapsed);
        CHECK(rig.state.die_temperature_valid);
    }

    rig.power.tick(t + go::PowerService::kDieStaleMs + kPeriod);
    CHECK_FALSE(rig.state.die_temperature_valid);
    CHECK(rig.state.die_decicelsius == 700);
}

TEST_CASE("thermal: a sensor that answers again is believed again") {
    Rig rig;
    uint32_t t = 1000;
    rig.power.tick(t);
    rig.sensor.answers = false;
    rig.power.tick(t + go::PowerService::kDieStaleMs + kPeriod);
    REQUIRE_FALSE(rig.state.die_temperature_valid);

    rig.sensor.answers = true;
    rig.sensor.decicelsius = 180;
    rig.power.tick(t + go::PowerService::kDieStaleMs + 2 * kPeriod);
    CHECK(rig.state.die_temperature_valid);
    CHECK(rig.state.die_decicelsius == 180);
}

TEST_CASE("thermal: the freshness window spans the 49.7-day wrap of the counter") {
    Rig rig;
    const uint32_t before_wrap = 0xFFFFFFFFu - kPeriod;
    rig.power.tick(before_wrap);
    REQUIRE(rig.state.die_temperature_valid);

    rig.sensor.answers = false;
    rig.power.tick(before_wrap + kPeriod);
    CHECK(rig.state.die_temperature_valid);

    rig.power.tick(before_wrap + go::PowerService::kDieStaleMs + 2 * kPeriod);
    CHECK_FALSE(rig.state.die_temperature_valid);
}

TEST_CASE("thermal: the supply warning the panel gate reads is the monitor's own") {
    Rig rig;
    rig.power.tick(1000);
    REQUIRE_FALSE(rig.state.supply_warned);

    rig.power.on_supply_warning();
    rig.power.tick(2000);
    CHECK(rig.state.supply_warned);
}
