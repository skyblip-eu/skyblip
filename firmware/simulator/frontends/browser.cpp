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
KEEPALIVE void simulator_set_position(int lat_1e7, int lon_1e7) {
    g_simulator.world().gnss().lat_1e7 = lat_1e7;
    g_simulator.world().gnss().lon_1e7 = lon_1e7;
}

KEEPALIVE void simulator_add_aircraft(int north_m, int east_m, int up_m, int speed_mps,
                                      int track_deg) {
    g_simulator.world().add_aircraft(north_m, east_m, up_m, speed_mps, track_deg);
}
KEEPALIVE void simulator_add_threat() { g_simulator.world().add_threat(); }
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
KEEPALIVE int simulator_speed_q() { return g_simulator.product().state().own.speed_q; }
KEEPALIVE int simulator_track_c9() { return g_simulator.product().state().own.track_c9; }
KEEPALIVE int simulator_climb_e8() { return g_simulator.product().state().own.climb_e8; }
KEEPALIVE int simulator_traffic_count() { return g_simulator.product().state().traffic.count(); }
KEEPALIVE int simulator_alarm_level() { return g_simulator.product().state().alarm_level; }
KEEPALIVE int simulator_vibro_ms() { return g_simulator.vibro_ms(); }
KEEPALIVE int simulator_rx_ok() { return static_cast<int>(g_simulator.product().state().rx_ok); }
KEEPALIVE int simulator_rx_bad() { return static_cast<int>(g_simulator.product().state().rx_bad); }
KEEPALIVE int simulator_failures() { return g_simulator.world().failures(); }
}

int main() { return 0; }
