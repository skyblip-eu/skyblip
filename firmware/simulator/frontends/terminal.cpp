#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>

#include "simulator/simulator.h"

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
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - t0).count());
}

const char* kPages[] = {"radar", "6-pack", "status"};
const char* kAlarm[] = {"none", "info", "IMPORTANT", "URGENT"};
const char* kModes[] = {"dev", "demo", "training"};

void render(simulator::Simulator& s) {
    if (!s.panel_powered()) {
        std::printf("\033[2J\033[H[e-paper POWERED OFF]  ([o] power on, [q] quit)\n");
        std::fflush(stdout);
        return;
    }
    const ui::Framebuffer& fb = s.panel();
    constexpr int step = 4;
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

    const bus::State& st = s.product().state();
    const messages::OwnState& o = st.own;
    std::printf(" mode:%-8s page:%-6s backlight:%-3s\n", kModes[static_cast<int>(s.mode())],
                kPages[static_cast<int>(s.product().screen().page())],
                s.backlight() ? "ON" : "off");
    std::printf("  fix:%-6s sats:%2u  pos:%.5f,%.5f\n", o.fix_valid ? "3D" : "none", o.sats,
                o.lat_1e7 / 1e7, o.lon_1e7 / 1e7);
    std::printf("  alt:%5dm  spd:%5.1fm/s  trk:%03u  vs:%+.1fm/s\n", o.alt_m, o.speed_q / 4.0,
                static_cast<unsigned>(o.track_c9 * 360 / 512), o.climb_e8 / 8.0);
    std::printf("  traffic:%d  rx ok/bad:%u/%u  ALARM:%s\n", st.traffic.count(), st.rx_ok,
                st.rx_bad, kAlarm[st.alarm_level > 3 ? 3 : st.alarm_level]);
    std::printf(" device: [p]age [b]acklight [o]n/off   sensors: [f]ix [n]o-pps\n");
    std::printf(" alt a/z  speed s/x  track d/c   traffic: [g]+1 [h]threat [k]clear  [q]uit\n");
    std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
    simulator::Simulator s;
    if (s.setup() != Status::Ok) {
        std::printf("simulator setup failed\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        if (std::strncmp(argv[i], "--scenario=", 11) != 0) continue;
        if (!s.load_file(argv[i] + 11)) {
            std::printf("cannot load scenario %s\n", argv[i] + 11);
            return 1;
        }
    }

    bool fix = true, pps = true;
    int32_t alt = 1000, spd = 45, trk = 90;

    TermRaw raw;
    bool running = true;
    while (running) {
        char c;
        while (read(STDIN_FILENO, &c, 1) == 1) {
            switch (c) {
                case 'q': running = false; break;
                case 'p': s.world().press_button(); break;
                case 'b': s.product().screen().set_backlight(!s.backlight()); break;
                case 'o': s.product().screen().set_power(!s.panel_powered()); break;
                case 'f':
                    fix = !fix;
                    s.world().set_fix(fix);
                    break;
                case 'n':
                    pps = !pps;
                    s.world().set_pps_locked(pps);
                    break;
                case 'a': s.world().set_altitude_m(alt += 100); break;
                case 'z': s.world().set_altitude_m(alt -= 100); break;
                case 's': s.world().set_speed_kt(spd += 5); break;
                case 'x': s.world().set_speed_kt(spd = spd > 5 ? spd - 5 : 0); break;
                case 'd': s.world().set_track_deg(trk = (trk + 15) % 360); break;
                case 'c': s.world().set_track_deg(trk = (trk + 345) % 360); break;
                case 'g': s.world().add_aircraft(1500, 800, 50, 30, 250); break;
                case 'h': s.world().add_threat(); break;
                case 'k': s.world().clear_aircraft(); break;
                default: break;
            }
        }
        s.step(now_ms());
        render(s);
        usleep(50 * 1000);
    }
    const bus::State& st = s.product().state();
    std::printf("\nbye — traffic=%d rx=%u failures=%d\n", st.traffic.count(), st.rx_ok,
                s.world().failures());
    return 0;
}
