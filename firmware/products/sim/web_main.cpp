// products/sim/web_main.cpp — BROWSER (WASM) frontend of the device simulator.
//
// Exposes the SimHarness control surface as C entry points that JavaScript
// calls; the framebuffer is read straight out of the WASM heap and painted onto
// a <canvas> by products/sim/web/index.html. Runs the REAL firmware in the
// browser — nothing to install for whoever opens the page.
//
// Build (needs Emscripten): `make web`  →  products/sim/web/skyblip_sim.{js,wasm}
#include "products/sim/harness.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define KEEPALIVE
#endif

using namespace skyblip;

namespace {
sim::SimHarness g_h;
}

extern "C" {

// ---- lifecycle -------------------------------------------------------------
KEEPALIVE void sim_setup() {
    g_h.setup();
    g_h.backlight(true);
}
KEEPALIVE void sim_step(unsigned ms) { g_h.step(ms); }

// ---- device controls -------------------------------------------------------
KEEPALIVE void sim_button() { g_h.button(); }
KEEPALIVE void sim_backlight(int on) { g_h.backlight(on != 0); }
KEEPALIVE void sim_power(int on) { g_h.power(on != 0); }
KEEPALIVE void sim_set_range(int m) { g_h.set_range_m(m); }

// ---- simulated GNSS / sensors ---------------------------------------------
KEEPALIVE void sim_set_fix(int on) { g_h.set_fix(on != 0); }
KEEPALIVE void sim_set_sats(int n) { g_h.set_sats(n); }
KEEPALIVE void sim_set_alt(int m) { g_h.set_altitude_m(m); }
KEEPALIVE void sim_set_speed(int kt) { g_h.set_speed_kt(kt); }
KEEPALIVE void sim_set_track(int deg) { g_h.set_track_deg(deg); }
KEEPALIVE void sim_set_climb(int e1) { g_h.set_climb_e1(e1); }
KEEPALIVE void sim_set_position(int lat_1e7, int lon_1e7) {
    g_h.gnss().lat_1e7 = lat_1e7;
    g_h.gnss().lon_1e7 = lon_1e7;
}

// ---- simulated traffic -----------------------------------------------------
KEEPALIVE void sim_add_aircraft(int north_m, int east_m, int up_m, int speed_mps, int track_deg) {
    g_h.add_aircraft(north_m, east_m, up_m, speed_mps, track_deg);
}
KEEPALIVE void sim_add_threat() { g_h.add_threat(); }
KEEPALIVE void sim_clear_traffic() { g_h.clear_aircraft(); }
KEEPALIVE int sim_aircraft_count() { return g_h.aircraft_count(); }

// ---- framebuffer -----------------------------------------------------------
// Layout: byte = fb[y*stride + (x>>3)], black if (byte & (0x80 >> (x&7))).
KEEPALIVE const unsigned char* sim_fb() { return g_h.framebuffer().data(); }
KEEPALIVE int sim_fb_w() { return ui::Framebuffer::kW; }
KEEPALIVE int sim_fb_h() { return ui::Framebuffer::kH; }
KEEPALIVE int sim_fb_stride() { return ui::Framebuffer::kStride; }

// ---- telemetry (what the firmware actually decoded) ------------------------
KEEPALIVE int sim_backlight_on() { return g_h.backlight_on() ? 1 : 0; }
KEEPALIVE int sim_powered() { return g_h.powered() ? 1 : 0; }
KEEPALIVE int sim_page() { return static_cast<int>(g_h.page()); }
KEEPALIVE int sim_fix_valid() { return g_h.own().fix_valid ? 1 : 0; }
KEEPALIVE int sim_sats() { return g_h.own().sats; }
KEEPALIVE int sim_lat_1e7() { return g_h.own().lat_1e7; }
KEEPALIVE int sim_lon_1e7() { return g_h.own().lon_1e7; }
KEEPALIVE int sim_alt_m() { return g_h.own().alt_m; }
KEEPALIVE int sim_speed_q() { return g_h.own().speed_q; }
KEEPALIVE int sim_track_c9() { return g_h.own().track_c9; }
KEEPALIVE int sim_climb_e8() { return g_h.own().climb_e8; }
KEEPALIVE int sim_traffic_count() { return g_h.traffic_count(); }
KEEPALIVE int sim_alarm_level() { return g_h.alarm_level(); }
KEEPALIVE int sim_rx_ok() { return static_cast<int>(g_h.rx_ok()); }
KEEPALIVE int sim_rx_bad() { return static_cast<int>(g_h.rx_bad()); }
}

// Present so a native compile-check (no Emscripten) links; harmless under WASM
// (EXIT_RUNTIME=0 keeps the module alive for the exported calls).
int main() { return 0; }
