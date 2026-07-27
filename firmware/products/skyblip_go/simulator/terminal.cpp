// products/skyblip_go/simulator/terminal.cpp — TERMINAL frontend.
// Renders the captured e-paper framebuffer as ASCII and maps keys to the device
// controls AND to the modelled sensors (GNSS + traffic). Thin: all behaviour
// lives in the virtual board next door (simulator/t_echo_plus.h).
//
//   make simulator && ./build/skyblip_simulator
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>

#include "products/skyblip_go/simulator/t_echo_plus.h"

using namespace skyblip;

namespace {

struct TermRaw {
    termios saved{};
    TermRaw() {
        tcgetattr(STDIN_FILENO, &saved);
        termios raw = saved;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
    }
    ~TermRaw() { tcsetattr(STDIN_FILENO, TCSANOW, &saved); }
};

uint32_t now_ms() {
    using namespace std::chrono;
    static auto t0 = steady_clock::now();
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}

const char* kPages[] = {"radar", "alt/vs", "status", "6-pack"};
const char* kAlarm[] = {"none", "info", "IMPORTANT", "URGENT"};

void render(simulator::TEchoPlus& h) {
    if (!h.powered()) {
        std::printf("\033[2J\033[H[e-paper POWERED OFF]  ([o] power on, [q] quit)\n");
        std::fflush(stdout);
        return;
    }
    const ui::Framebuffer& fb = h.framebuffer();
    constexpr int step = 4;  // 200/4 = 50x50 cells
    std::printf("\033[2J\033[H+----------------------------------------------------+\n");
    for (int y = 0; y < ui::Framebuffer::kH; y += step) {
        std::printf("|");
        for (int x = 0; x < ui::Framebuffer::kW; x += step) {
            bool black = false;
            for (int dy = 0; dy < step && !black; dy++)
                for (int dx = 0; dx < step && !black; dx++)
                    if (fb.get_pixel(x + dx, y + dy)) black = true;
            std::printf("%c", black ? '#' : ' ');
        }
        std::printf("|\n");
    }
    std::printf("+----------------------------------------------------+\n");

    const messages::OwnState& o = h.own();
    std::printf(" page:%-6s backlight:%-3s   DECODED BY FIRMWARE:\n",
                kPages[static_cast<int>(h.page())], h.backlight_on() ? "ON" : "off");
    std::printf("  fix:%-6s sats:%2u  pos:%.5f,%.5f\n", o.fix_valid ? "3D" : "none", o.sats,
                o.lat_1e7 / 1e7, o.lon_1e7 / 1e7);
    std::printf("  alt:%5dm  spd:%5.1fm/s  trk:%03u  vs:%+.1fm/s\n", o.alt_m, o.speed_q / 4.0,
                (unsigned)(o.track_c9 * 360 / 512), o.climb_e8 / 8.0);
    std::printf("  traffic:%d  rx ok/bad:%u/%u  ALARM:%s\n", h.traffic_count(), h.rx_ok(),
                h.rx_bad(), kAlarm[h.alarm_level() > 3 ? 3 : h.alarm_level()]);
    std::printf(" device: [p]age [b]acklight [o]n/off   sensors: [f]ix\n");
    std::printf(" alt a/z  speed s/x  track d/c   traffic: [g]+1 [h]threat [k]clear  [q]uit\n");
    std::fflush(stdout);
}

}  // namespace

int main() {
    simulator::TEchoPlus h;
    if (h.setup() != Status::Ok) {
        std::printf("simulator setup failed\n");
        return 1;
    }
    h.backlight(true);

    // Local mirrors of the sensor sliders (the virtual board owns the truth).
    bool fix = true;
    int32_t alt = 1000, spd = 45, trk = 90;

    TermRaw raw;
    bool running = true;
    while (running) {
        char c;
        while (read(STDIN_FILENO, &c, 1) == 1) {
            switch (c) {
                case 'q': running = false; break;
                // device controls
                case 'p': h.button(); break;
                case 'b': h.backlight(!h.backlight_on()); break;
                case 'o': h.power(!h.powered()); break;
                // modelled GNSS
                case 'f':
                    fix = !fix;
                    h.set_fix(fix);
                    break;
                case 'a':
                    alt += 100;
                    h.set_altitude_m(alt);
                    break;
                case 'z':
                    alt -= 100;
                    h.set_altitude_m(alt);
                    break;
                case 's':
                    spd += 5;
                    h.set_speed_kt(spd);
                    break;
                case 'x':
                    spd = spd > 5 ? spd - 5 : 0;
                    h.set_speed_kt(spd);
                    break;
                case 'd':
                    trk = (trk + 15) % 360;
                    h.set_track_deg(trk);
                    break;
                case 'c':
                    trk = (trk + 345) % 360;
                    h.set_track_deg(trk);
                    break;
                // virtual traffic
                case 'g': h.add_aircraft(1500, 800, 50, 30, 250); break;
                case 'h': h.add_threat(); break;
                case 'k': h.clear_aircraft(); break;
                default: break;
            }
        }
        h.step(now_ms());
        render(h);
        usleep(50 * 1000);  // ~20 Hz
    }
    std::printf("\nbye — page=%s traffic=%d rx=%u\n", kPages[static_cast<int>(h.page())],
                h.traffic_count(), h.rx_ok());
    return 0;
}
