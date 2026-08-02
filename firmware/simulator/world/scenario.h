#ifndef SKYBLIP_SIMULATOR_WORLD_SCENARIO_H
#define SKYBLIP_SIMULATOR_WORLD_SCENARIO_H

#include <cstdint>
#include <string>
#include <vector>

#include "core/protocol/air.h"

namespace skyblip::simulator {

enum class Mode : uint8_t { Dev, Demo, Training };

enum class EventKind : uint8_t {
    Fix,
    Pps,
    Baro,
    BatteryMv,
    ExternalPower,
    Button,
    Altitude,
    Track,
    Aircraft,
    ExpectAlarmMin,
    ExpectTrafficMin,
};

struct ScenarioAircraft {
    // "adsl" or "alptas": what this aircraft is equipped with, which is the only
    // thing that decides whether our shared sync window frames its bursts.
    protocol::System system{protocol::System::AdslDirect};
    double north_m{0};
    double east_m{0};
    double up_m{0};
    double speed_mps{30};
    double track_deg{270};
    // Where in the second this aircraft transmits and on which M-band channel.
    // Left alone it behaves like a conforming transmitter. Pinned, it is how a
    // slot-timing bug becomes a committed fixture.
    int phase_ms{-1};
    int slot{-1};
};

struct ScenarioEvent {
    uint32_t at_ms{0};
    EventKind kind{EventKind::Fix};
    long value{0};
};

// One scenario file drives the browser, the terminal and the regression tests, so
// a bug found in flight becomes a committed fixture rather than a bug report.
struct Scenario {
    std::string name;
    Mode mode{Mode::Dev};
    int32_t lat_1e7{485000000};
    int32_t lon_1e7{85000000};
    int32_t alt_m{1000};
    int32_t speed_kt{45};
    int32_t track_deg{90};
    uint32_t duration_ms{0};
    std::vector<ScenarioAircraft> aircraft;
    std::vector<ScenarioEvent> events;
};

bool parse_scenario(const char* json, int len, Scenario& out);
bool load_scenario(const char* path, Scenario& out);

}  // namespace skyblip::simulator

#endif
