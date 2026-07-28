#ifndef SKYBLIP_SIMULATOR_WORLD_WORLD_H
#define SKYBLIP_SIMULATOR_WORLD_WORLD_H

#include "core/bus/state.h"
#include "hardware/platform/host/platform.h"
#include "simulator/world/scenario.h"

namespace skyblip::simulator {

struct VirtualAircraft {
    bool used{false};
    uint32_t addr{0};
    double north_m{0}, east_m{0}, up_m{0};
    double speed_mps{30};
    double track_deg{270};
    int32_t climb_e8{0};
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
                     double track_deg = 270);
    int add_threat() { return add_aircraft(600, 200, 30, 40, 200); }
    void clear_aircraft();
    int aircraft_count() const;

    models::L76k& gnss() { return platform_.chips().gnss; }
    models::Bme280& baro() { return platform_.baro().chip; }
    platform::host::Platform& platform() { return platform_; }

    void set_fix(bool on) { gnss().fix = on; }
    void set_sats(int n) { gnss().sats = static_cast<uint8_t>(n < 0 ? 0 : (n > 32 ? 32 : n)); }
    void set_altitude_m(int32_t m) { gnss().alt_m = m; }
    void set_speed_kt(int32_t kt) { gnss().speed_kt = kt; }
    void set_track_deg(int32_t deg) { gnss().track_deg = ((deg % 360) + 360) % 360; }
    void set_climb_e1(int32_t e1) { gnss().climb_mps_e1 = e1; }
    void set_pps_locked(bool on) { platform_.pps().set_locked(on); }
    void press_button() { press_pending_ = true; }

    int failures() const { return failures_; }
    const char* first_failure() const { return failure_[0] == 0 ? nullptr : failure_; }
    bool finished(uint32_t now_ms) const {
        return scenario_.duration_ms != 0 && now_ms - start_ms_ >= scenario_.duration_ms;
    }

   private:
    void service_button(uint32_t now_ms);
    void service_aircraft(uint32_t now_ms, const messages::OwnState& own);
    void transmit(const VirtualAircraft& aircraft, const messages::OwnState& own);
    void apply_events(uint32_t now_ms, const bus::State& state);
    void fail(const char* what);

    // A modelled press has to last longer than ui::Button's debounce window or
    // the firmware is right to ignore it.
    static constexpr uint32_t kPressMs = 60;
    static constexpr uint32_t kBaroPeriodMs = 250;

    platform::host::Platform& platform_;
    VirtualAircraft aircraft_[kMaxAircraft]{};
    Scenario scenario_{};
    size_t next_event_{0};
    uint32_t start_ms_{0};
    uint32_t last_aircraft_ms_{0};
    uint32_t last_baro_ms_{0};
    uint32_t press_until_ms_{0};
    int round_robin_{0};
    int failures_{0};
    bool armed_{false};
    bool press_pending_{false};
    char failure_[96]{0};
};

}  // namespace skyblip::simulator

#endif
