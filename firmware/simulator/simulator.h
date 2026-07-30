#ifndef SKYBLIP_SIMULATOR_SIM_H
#define SKYBLIP_SIMULATOR_SIM_H

#include "hardware/platform/host/platform.h"
#include "products/skyblip_go/product.h"
#include "simulator/world/world.h"

namespace skyblip::simulator {

// A product on the host platform, inside a world. The frontends add pixels and
// controls; the tests add assertions. Neither adds behaviour.
class Simulator {
   public:
    using Product = go::Product<platform::host::Platform>;

    Status setup() { return product_.setup(); }

    void step(uint32_t now_ms) {
        platform_.clock().set_millis(now_ms);
        world_.step(now_ms, product_.state());
        product_.step(now_ms);
    }

    // A step is how finely the one host loop stands in for the radio thread the
    // silicon platform runs: coarser than the 5 ms guard on the O->M edge and
    // the simulation, not the firmware, is what misses a slot boundary.
    static constexpr uint32_t kStepMs = 5;

    void run(uint32_t until_ms, uint32_t step_ms = kStepMs) {
        for (uint32_t t = 0; t <= until_ms; t += step_ms) step(t);
    }

    void load(const Scenario& scenario) {
        scenario_ = scenario;
        world_.load(scenario);
    }

    bool load_file(const char* path) {
        Scenario s;
        if (!load_scenario(path, s)) return false;
        load(s);
        return true;
    }

    Mode mode() const { return scenario_.mode; }
    const Scenario& scenario() const { return scenario_; }

    Product& product() { return product_; }
    World& world() { return world_; }
    platform::host::Platform& platform() { return platform_; }

    const ui::Framebuffer& panel() { return platform_.chips().epd.framebuffer(); }
    bool panel_powered() { return platform_.chips().epd.powered; }
    bool backlight() { return platform_.chips().epd.backlight; }
    int present_count() { return platform_.chips().epd.present_count; }
    uint8_t alarm_level() { return platform_.annunciator().level(); }
    uint16_t vibro_ms() { return platform_.annunciator().vibro_ms(); }

   private:
    platform::host::Platform platform_{};
    Product product_{platform_};
    World world_{platform_};
    Scenario scenario_{};
};

}  // namespace skyblip::simulator

#endif
