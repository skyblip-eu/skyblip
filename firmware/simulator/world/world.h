#ifndef SKYBLIP_SIMULATOR_WORLD_WORLD_H
#define SKYBLIP_SIMULATOR_WORLD_WORLD_H

#include "core/bus/state.h"
#include "core/flight/atmosphere.h"
#include "core/timing/slot.h"
#include "hardware/platform/host/platform.h"
#include "simulator/world/air.h"
#include "simulator/world/scenario.h"

namespace skyblip::simulator {

struct VirtualAircraft {
    bool used{false};
    // Which system this aircraft is equipped with. Both are Manchester bursts on
    // the same two M-band channels, which is exactly why one dwell can hear them.
    protocol::System system{protocol::System::AdslDirect};
    uint32_t addr{0};
    // North and east of the world's origin, not of own-ship: two aircraft that
    // both manoeuvre only describe one encounter if they fly over the same
    // ground. up_m stays own-relative, as the air in one thermal is.
    double north_m{0}, east_m{0}, up_m{0};
    double speed_mps{30};
    double track_deg{270};
    // Degrees per second, positive to the right: a target holding a steady turn,
    // which is what a glider in a thermal is doing.
    double turn_dps{0};
    int32_t climb_e8{0};
    // Where in the second this aircraft transmits, and on which M-band channel.
    // Below zero it picks its own instant per second the way a conforming
    // transmitter does. Pinned, it is the knob that proves our dwell map.
    int phase_ms{-1};
    int slot{-1};
    uint32_t transmissions{0};
};

// The sky, the air and the ground the firmware flies through. It drives the part
// models only: virtual aircraft are broadcast as genuine scrambled ADS-L frames
// and own-ship motion as genuine NMEA, so the production receive and parse paths
// are what run.
class World {
   public:
    static constexpr int kMaxAircraft = 8;

    explicit World(platform::host::Platform& platform) : platform_(platform) {}

    void step(uint32_t now_ms, const bus::State& state);

    void load(const Scenario& scenario);

    int add_aircraft(double north_m, double east_m, double up_m, double speed_mps = 30,
                     double track_deg = 270, int phase_ms = -1, int slot = -1,
                     protocol::System system = protocol::System::AdslDirect, double turn_dps = 0);
    int add_threat(protocol::System system = protocol::System::AdslDirect) {
        return add_aircraft(600, 200, 30, 40, 200, -1, -1, system);
    }
    void clear_aircraft();
    int aircraft_count() const;
    // Where the world says the two aircraft actually are, which is not what the
    // firmware knows: the device only ever has the target's last report.
    const VirtualAircraft* aircraft_at(int index) const;
    double separation_m(int index) const;

    Air& air() { return air_; }
    models::L76k& gnss() { return platform_.chips().gnss; }
    const models::L76k& gnss() const { return platform_.chips().gnss; }
    models::Bme280& baro() { return platform_.baro().chip; }
    platform::host::Platform& platform() { return platform_; }

    void set_fix(bool on) { gnss().fix = on; }
    void set_sats(int n) { gnss().sats = static_cast<uint8_t>(n < 0 ? 0 : (n > 32 ? 32 : n)); }
    void set_altitude_m(int32_t m) { gnss().alt_m = m; }
    void set_speed_kt(int32_t kt) { gnss().speed_kt = kt; }
    void set_track_deg(int32_t deg) { gnss().track_deg = ((deg % 360) + 360) % 360; }
    void set_climb_e1(int32_t e1) { gnss().climb_mps_e1 = e1; }
    // The weather, not a setting: the sea-level pressure of the air the aircraft
    // is flying through. The barometer reads what that implies at its altitude.
    void set_airmass_qnh_pa(uint32_t pa) { airmass_qnh_pa_ = pa; }
    void set_pps_locked(bool on) { platform_.pps().set_locked(on); }
    // The cell as the world holds it: what the divider reads, and whether a
    // cable is in. Everything else about the battery is the firmware's opinion.
    void set_battery_mv(int32_t mv) {
        platform_.battery().millivolts = static_cast<uint16_t>(mv < 0 ? 0 : mv);
    }
    void set_external_power(bool on) { platform_.battery().external_power = on; }
    void press_button() { press_pending_ = true; }

    int failures() const { return failures_; }
    const char* first_failure() const { return failure_[0] == 0 ? nullptr : failure_; }
    bool finished(uint32_t now_ms) const {
        return scenario_.duration_ms != 0 && now_ms - start_ms_ >= scenario_.duration_ms;
    }

   private:
    void service_button(uint32_t now_ms);
    void set_origin();
    double own_north_m() const;
    double own_east_m() const;
    void service_aircraft(uint32_t now_ms, const messages::OwnState& own);
    void schedule_second(uint64_t epoch_us, const messages::OwnState& own);
    void transmit(VirtualAircraft& aircraft, uint64_t epoch_us, const messages::OwnState& own);
    static int8_t rssi_at(double range_m);
    void apply_events(uint32_t now_ms, const bus::State& state);
    void fail(const char* what);

    // A modelled press has to last longer than ui::Button's debounce window or
    // the firmware is right to ignore it.
    static constexpr uint32_t kPressMs = 60;
    static constexpr uint32_t kBaroPeriodMs = 250;

    platform::host::Platform& platform_;
    Air air_{};
    VirtualAircraft aircraft_[kMaxAircraft]{};
    Scenario scenario_{};
    size_t next_event_{0};
    uint32_t start_ms_{0};
    uint32_t last_aircraft_ms_{0};
    uint32_t last_baro_ms_{0};
    uint32_t airmass_qnh_pa_{flight::kIsaSeaLevelPa};
    uint32_t press_until_ms_{0};
    int32_t origin_lat_1e7_{0};
    int32_t origin_lon_1e7_{0};
    bool origin_set_{false};
    uint32_t scheduled_sec_{0};
    bool scheduled_{false};
    int failures_{0};
    bool armed_{false};
    bool press_pending_{false};
    char failure_[96]{0};
};

}  // namespace skyblip::simulator

#endif
