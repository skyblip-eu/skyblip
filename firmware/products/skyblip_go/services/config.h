#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_CONFIG_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_CONFIG_H

#include "core/comms/config.h"
#include "runtime/service.h"

namespace skyblip::go {

// The companion link's device side: load and persist settings, feed the config
// state machine, and take the running image off probation once the hardware has
// proved itself.
class ConfigLinkService : public runtime::Service {
   public:
    explicit ConfigLinkService(runtime::Context& ctx)
        : runtime::Service(ctx),
          config_(ctx.roles.link, ctx.state.settings, &ctx.roles.dfu, &ctx.state.timing_stats) {}

    Status setup() override;
    void tick(uint32_t now_ms) override;

    comms::ConfigService& config() { return config_; }

   private:
    void load();
    void persist();
    void confirm_image_once_healthy();

    comms::ConfigService config_;
    bool image_confirmed_{false};

    static constexpr size_t kBlobCap = 64;
};

}  // namespace skyblip::go

#endif
