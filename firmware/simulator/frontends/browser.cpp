#include "simulator/simulator.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define KEEPALIVE
#endif

using namespace skyblip;

namespace {
simulator::Simulator g_simulator;
}

extern "C" {

KEEPALIVE void simulator_setup() { g_simulator.setup(); }
KEEPALIVE void simulator_step(unsigned ms) { g_simulator.step(ms); }
KEEPALIVE int simulator_load_scenario(const char* json, int len) {
    simulator::Scenario s;
    if (!simulator::parse_scenario(json, len, s)) return 0;
    g_simulator.load(s);
    return 1;
}
KEEPALIVE int simulator_mode() { return static_cast<int>(g_simulator.mode()); }

KEEPALIVE void simulator_button() { g_simulator.world().press_button(); }
// The capacitive pad on P0.11 is not an input the firmware polls: on this case
// it asks for a repaint, which is a full refresh of the panel.
KEEPALIVE void simulator_touch() { g_simulator.product().screen().mark_dirty(); }
KEEPALIVE void simulator_backlight(int on) {
    g_simulator.product().screen().set_backlight(on != 0);
}
KEEPALIVE void simulator_power(int on) { g_simulator.product().screen().set_power(on != 0); }
KEEPALIVE void simulator_set_range(int m) { g_simulator.product().screen().set_range_m(m); }

KEEPALIVE void simulator_set_fix(int on) { g_simulator.world().set_fix(on != 0); }
KEEPALIVE void simulator_set_pps(int on) { g_simulator.world().set_pps_locked(on != 0); }
KEEPALIVE void simulator_set_sats(int n) { g_simulator.world().set_sats(n); }
KEEPALIVE void simulator_set_alt(int m) { g_simulator.world().set_altitude_m(m); }
KEEPALIVE void simulator_set_speed(int kt) { g_simulator.world().set_speed_kt(kt); }
KEEPALIVE void simulator_set_track(int deg) { g_simulator.world().set_track_deg(deg); }
KEEPALIVE void simulator_set_climb(int e1) { g_simulator.world().set_climb_e1(e1); }
// Both in pascals: the subscale the device is set to, and the air outside.
KEEPALIVE void simulator_set_qnh(int pa) {
    g_simulator.product().state().qnh_pa = static_cast<uint32_t>(pa);
    g_simulator.product().screen().mark_dirty();
}
KEEPALIVE void simulator_set_airmass(int pa) {
    g_simulator.world().set_airmass_qnh_pa(static_cast<uint32_t>(pa));
}
// The cell and the cable: the same two facts the board can read on silicon.
KEEPALIVE void simulator_set_battery_mv(int mv) { g_simulator.world().set_battery_mv(mv); }
KEEPALIVE void simulator_set_external_power(int on) {
    g_simulator.world().set_external_power(on != 0);
}
KEEPALIVE void simulator_set_position(int lat_1e7, int lon_1e7) {
    g_simulator.world().gnss().lat_1e7 = lat_1e7;
    g_simulator.world().gnss().lon_1e7 = lon_1e7;
}

// alptas picks what the aircraft is equipped with: 0 is ADS-L, anything else is
// ALP-TAS. Both land on the same two M-band channels, so it is the shared sync
// window that decides whether we hear it, not the button.
static protocol::System system_of(int alptas) {
    return alptas != 0 ? protocol::System::Alptas : protocol::System::AdslDirect;
}

KEEPALIVE void simulator_add_aircraft(int north_m, int east_m, int up_m, int speed_mps,
                                      int track_deg, int alptas) {
    g_simulator.world().add_aircraft(north_m, east_m, up_m, speed_mps, track_deg, -1, -1,
                                     system_of(alptas));
}
// phase_ms/slot below zero let the aircraft pick its own instant, as a
// conforming transmitter does; pinned, they put a burst where the dwell map
// says we should or should not hear it.
KEEPALIVE void simulator_add_aircraft_at(int north_m, int east_m, int up_m, int speed_mps,
                                         int track_deg, int phase_ms, int slot, int alptas) {
    g_simulator.world().add_aircraft(north_m, east_m, up_m, speed_mps, track_deg, phase_ms, slot,
                                     system_of(alptas));
}
KEEPALIVE void simulator_add_threat(int alptas) {
    g_simulator.world().add_threat(system_of(alptas));
}
KEEPALIVE void simulator_clear_traffic() { g_simulator.world().clear_aircraft(); }
KEEPALIVE int simulator_aircraft_count() { return g_simulator.world().aircraft_count(); }

// Layout: byte = fb[y*stride + (x>>3)], black if (byte & (0x80 >> (x&7))).
KEEPALIVE const unsigned char* simulator_fb() { return g_simulator.panel().data(); }
KEEPALIVE int simulator_fb_w() { return ui::Framebuffer::kW; }
KEEPALIVE int simulator_fb_h() { return ui::Framebuffer::kH; }
KEEPALIVE int simulator_fb_stride() { return ui::Framebuffer::kStride; }

KEEPALIVE int simulator_backlight_on() { return g_simulator.backlight() ? 1 : 0; }
KEEPALIVE int simulator_powered() { return g_simulator.panel_powered() ? 1 : 0; }
KEEPALIVE int simulator_page() { return static_cast<int>(g_simulator.product().screen().page()); }
KEEPALIVE int simulator_capabilities() {
    return static_cast<int>(g_simulator.product().capabilities());
}
KEEPALIVE int simulator_fix_valid() { return g_simulator.product().state().own.fix_valid ? 1 : 0; }
KEEPALIVE int simulator_sats() { return g_simulator.product().state().own.sats; }
KEEPALIVE int simulator_lat_1e7() { return g_simulator.product().state().own.lat_1e7; }
KEEPALIVE int simulator_lon_1e7() { return g_simulator.product().state().own.lon_1e7; }
KEEPALIVE int simulator_alt_m() { return g_simulator.product().state().own.alt_m; }
KEEPALIVE int simulator_pressure_pa() {
    return static_cast<int>(g_simulator.product().state().pressure_pa);
}
KEEPALIVE int simulator_battery_mv() { return g_simulator.product().state().battery.millivolts; }
KEEPALIVE int simulator_battery_percent() { return g_simulator.product().state().battery.percent; }
KEEPALIVE int simulator_battery_charging() {
    return g_simulator.product().state().battery.charging ? 1 : 0;
}
KEEPALIVE int simulator_speed_q() { return g_simulator.product().state().own.speed_q; }
KEEPALIVE int simulator_track_c9() { return g_simulator.product().state().own.track_c9; }
KEEPALIVE int simulator_climb_e8() { return g_simulator.product().state().own.climb_e8; }
KEEPALIVE int simulator_traffic_count() { return g_simulator.product().state().traffic.count(); }
KEEPALIVE int simulator_alarm_level() { return g_simulator.product().state().alarm_level; }
KEEPALIVE int simulator_vibro_ms() { return g_simulator.vibro_ms(); }
KEEPALIVE int simulator_rx_ok() { return static_cast<int>(g_simulator.product().state().rx_ok); }
KEEPALIVE int simulator_rx_bad() { return static_cast<int>(g_simulator.product().state().rx_bad); }
KEEPALIVE int simulator_tx_ok() { return static_cast<int>(g_simulator.product().state().tx_ok); }
KEEPALIVE int simulator_tx_busy() {
    return static_cast<int>(g_simulator.product().state().tx_busy);
}
KEEPALIVE int simulator_slot_state() {
    return static_cast<int>(g_simulator.product().state().plan.state);
}
KEEPALIVE int simulator_dwell_freq() {
    return static_cast<int>(g_simulator.product().state().plan.freq_hz / 1000);
}

// The tape: every burst that was on the air, heard or not.
KEEPALIVE int simulator_air_count() { return g_simulator.world().air().record_count(); }
KEEPALIVE int simulator_air_phase_ms(int i) { return g_simulator.world().air().record(i).phase_ms; }
KEEPALIVE int simulator_air_event(int i) {
    return static_cast<int>(g_simulator.world().air().record(i).event);
}
KEEPALIVE int simulator_air_freq_khz(int i) {
    return static_cast<int>(g_simulator.world().air().record(i).freq_hz / 1000);
}
KEEPALIVE int simulator_air_rssi(int i) { return g_simulator.world().air().record(i).rssi_dbm; }
KEEPALIVE const char* simulator_air_line(int i) {
    static char line[160];
    g_simulator.world().air().format(i, line, sizeof(line));
    return line;
}
KEEPALIVE int simulator_air_deaf() { return static_cast<int>(g_simulator.world().air().deaf()); }
KEEPALIVE int simulator_air_collisions() {
    return static_cast<int>(g_simulator.world().air().collisions());
}
KEEPALIVE int simulator_failures() { return g_simulator.world().failures(); }
}

int main() { return 0; }
