#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_CONFIG_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_CONFIG_H

#include "core/comms/config.h"
#include "core/timing/durable_write.h"
#include "runtime/service.h"

namespace skyblip::go {

// The companion link's device side: load and persist settings, feed the config
// state machine, and take the running image off probation once the hardware has
// proved itself.
//
// The persist half is why core/timing::DurableWriteWindow exists. A settings
// change is now a request, not a call: this service is the only writer of the
// blob, so it is also the only place that has to place the NVMC stall somewhere
// the radio can afford it. The pilot's side of that is invisible - the panel has
// already shown the new value, because state.settings changed when they changed
// it; what waits is the flash.
class ConfigLinkService : public runtime::Service {
   public:
    explicit ConfigLinkService(runtime::Context& ctx)
        : runtime::Service(ctx),
          config_(ctx.roles.link, ctx.state.settings, &ctx.roles.dfu, &ctx.state.timing_stats) {
        config_.set_durable_writes(&writes_);
    }

    Status setup() override;
    void tick(uint32_t now_ms) override;

    comms::ConfigService& config() { return config_; }
    const timing::DurableWriteWindow& durable_writes() const { return writes_; }

    // The one caller that may skip the window: the shutdown sequencer, once the
    // radio has been aborted. The bound above answers the cell that dies without
    // warning; this answers the power-off that does give warning, because losing a
    // change a pilot made to a deliberate shutdown would be the same broken
    // promise for no reason at all.
    void flush_settings(uint32_t now_ms);

   private:
    static constexpr size_t kBlobCap = 64;

    void load();
    void take_request(uint32_t now_ms);
    void drain_settings(uint32_t now_ms);
    void persist();
    void confirm_image_once_healthy();

    comms::ConfigService config_;
    timing::DurableWriteWindow writes_{};
    // The blob as flash already holds it. A pilot who steps a value up and back
    // down again has changed nothing, and NVS charges for a write either way:
    // this is what makes that free, and what keeps the sector - and so the
    // garbage collection the whole budget above is sized against - from filling
    // for no reason.
    uint8_t stored_[kBlobCap]{};
    size_t stored_len_{0};
    bool image_confirmed_{false};
};

}  // namespace skyblip::go

#endif
