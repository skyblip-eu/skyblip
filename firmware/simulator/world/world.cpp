#include "simulator/world/world.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/protocol/adsl.h"

namespace skyblip::simulator {

namespace {
constexpr double kPi = 3.14159265358979;
constexpr double kMetresPerDegLat = 111320.0;
}  // namespace

void World::step(uint32_t now_ms, const bus::State& state) {
    if (!armed_) {
        armed_ = true;
        start_ms_ = now_ms;
    }
    gnss().tick(now_ms);

    // One altitude, two sensors: the GNSS model integrates climb into its own
    // altitude and the barometer reads THAT, exactly as both would see the same
    // air. Setting them apart would show a climb rate no real pair can produce.
    // The airmass shifts the pressure the sensor sees, as weather does, without
    // moving the aircraft.
    if (now_ms - last_baro_ms_ >= kBaroPeriodMs) {
        last_baro_ms_ = now_ms;
        baro().set_pressure_pa(flight::alt_cm_to_pressure(
            gnss().alt_m * 100 + flight::pressure_to_alt_cm(airmass_qnh_pa_)));
    }

    service_button(now_ms);
    service_aircraft(now_ms, state.own);
    apply_events(now_ms, state);
}

void World::service_button(uint32_t now_ms) {
    if (press_pending_) {
        press_pending_ = false;
        press_until_ms_ = now_ms + kPressMs;
    }
    platform_.board_gpio().button_down = now_ms < press_until_ms_;
}

int World::add_aircraft(double north_m, double east_m, double up_m, double speed_mps,
                        double track_deg) {
    for (int i = 0; i < kMaxAircraft; i++) {
        if (aircraft_[i].used) continue;
        aircraft_[i] = VirtualAircraft{};
        aircraft_[i].used = true;
        aircraft_[i].addr = 0x300000u + static_cast<uint32_t>(i) + 1u;
        aircraft_[i].north_m = north_m;
        aircraft_[i].east_m = east_m;
        aircraft_[i].up_m = up_m;
        aircraft_[i].speed_mps = speed_mps;
        aircraft_[i].track_deg = track_deg;
        return i;
    }
    return -1;
}

void World::clear_aircraft() {
    for (auto& a : aircraft_) a.used = false;
}

int World::aircraft_count() const {
    int n = 0;
    for (const auto& a : aircraft_)
        if (a.used) n++;
    return n;
}

void World::service_aircraft(uint32_t now_ms, const messages::OwnState& own) {
    if (now_ms - last_aircraft_ms_ < 100) return;
    const double dt = (now_ms - last_aircraft_ms_) / 1000.0;
    last_aircraft_ms_ = now_ms;

    for (auto& a : aircraft_) {
        if (!a.used) continue;
        const double rad = a.track_deg * kPi / 180.0;
        a.north_m += a.speed_mps * std::cos(rad) * dt;
        a.east_m += a.speed_mps * std::sin(rad) * dt;
    }
    for (int k = 0; k < kMaxAircraft; k++) {
        round_robin_ = (round_robin_ + 1) % kMaxAircraft;
        if (aircraft_[round_robin_].used) {
            transmit(aircraft_[round_robin_], own);
            return;
        }
    }
}

void World::transmit(const VirtualAircraft& a, const messages::OwnState& own) {
    const double coslat = std::cos(own.lat_1e7 / 1e7 * kPi / 180.0);
    int32_t lat = own.lat_1e7 + static_cast<int32_t>(a.north_m * 1e7 / kMetresPerDegLat);
    int32_t lon = own.lon_1e7;
    if (coslat > 0.01) lon += static_cast<int32_t>(a.east_m * 1e7 / (kMetresPerDegLat * coslat));

    protocol::AdslPacket p;
    p.init();
    p.set_address(a.addr);
    p.set_addr_table(6);
    p.TimeStamp = static_cast<uint8_t>((last_aircraft_ms_ / 250) & 0x3F);
    p.FlightState = 2;
    p.AcftCat = 4;
    p.Emergency = 1;
    p.set_lat_1e7(lat);
    p.set_lon_1e7(lon);
    p.set_alt_m(own.alt_m + static_cast<int32_t>(a.up_m));
    p.set_speed_q(static_cast<uint16_t>(a.speed_mps * 4));
    p.set_climb_e8(static_cast<int16_t>(a.climb_e8));
    p.set_track_c9(static_cast<uint16_t>(a.track_deg * 512 / 360) & 0x1FF);
    p.SourceIntegrity = 3;
    p.DesignAssurance = 2;
    p.NavigIntegrity = 11;
    p.HorizAccuracy = 6;
    p.VertAccuracy = 3;
    p.VelAccuracy = 2;
    p.scramble();
    p.set_crc();

    uint8_t wire[protocol::AdslPacket::kTxBytes];
    std::memcpy(wire, &p, sizeof(wire));
    platform_.chips().radio.queue_rx(wire, static_cast<uint8_t>(sizeof(wire)));
}

void World::load(const Scenario& scenario) {
    scenario_ = scenario;
    next_event_ = 0;
    failures_ = 0;
    failure_[0] = 0;
    armed_ = false;

    gnss().lat_1e7 = scenario.lat_1e7;
    gnss().lon_1e7 = scenario.lon_1e7;
    gnss().alt_m = scenario.alt_m;
    gnss().speed_kt = scenario.speed_kt;
    gnss().track_deg = scenario.track_deg;

    clear_aircraft();
    for (const ScenarioAircraft& a : scenario.aircraft)
        add_aircraft(a.north_m, a.east_m, a.up_m, a.speed_mps, a.track_deg);
}

void World::apply_events(uint32_t now_ms, const bus::State& state) {
    const uint32_t elapsed = now_ms - start_ms_;
    while (next_event_ < scenario_.events.size() &&
           scenario_.events[next_event_].at_ms <= elapsed) {
        const ScenarioEvent& e = scenario_.events[next_event_++];
        switch (e.kind) {
            case EventKind::Fix: set_fix(e.value != 0); break;
            case EventKind::Pps: set_pps_locked(e.value != 0); break;
            case EventKind::Baro: platform_.baro().present = e.value != 0; break;
            case EventKind::Button: press_button(); break;
            case EventKind::Altitude: set_altitude_m(static_cast<int32_t>(e.value)); break;
            case EventKind::Track: set_track_deg(static_cast<int32_t>(e.value)); break;
            case EventKind::Aircraft: add_threat(); break;
            case EventKind::ExpectAlarmMin:
                if (state.alarm_level < e.value) fail("alarm level below expectation");
                break;
            case EventKind::ExpectTrafficMin:
                if (state.traffic.count() < e.value) fail("traffic count below expectation");
                break;
        }
    }
}

void World::fail(const char* what) {
    failures_++;
    if (failure_[0] == 0) std::snprintf(failure_, sizeof(failure_), "%s", what);
}

}  // namespace skyblip::simulator
