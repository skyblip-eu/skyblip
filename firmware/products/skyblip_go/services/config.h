#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_CONFIG_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_CONFIG_H

#include "core/comms/config.h"
#include "core/power/cutoff.h"
#include "core/timing/durable_write.h"
#include "products/skyblip_go/services/power.h"
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
//
// The other half is core/power's write rule. Below the low-battery warning the
// settings sector is not touched: NVS survives an interrupted write by design,
// but the sector it garbage-collects is the internal flash the running image
// executes from, and the moment a write lands is the moment a 22 dBm burst sags a
// 3.3 V cell. So a change arriving from a phone is refused at the door, out loud,
// with the reason on the link (core/comms/config.cpp), and a change already
// pending when the cell falls through the warning is HELD rather than dropped: it
// stays dirty, so a charger arriving still saves it, and the refusals are counted
// and readable over the same link. What is lost is one setting a pilot can make
// again; what is protected is every setting they ever made.
class ConfigLinkService : public runtime::Service {
   public:
    ConfigLinkService(runtime::Context& ctx, const PowerService& power)
        : runtime::Service(ctx),
          config_(ctx.roles.link, ctx.state.settings, &ctx.roles.dfu, &ctx.state.timing_stats),
          power_(power) {
        config_.set_durable_writes(&writes_);
    }

    Status setup() override;
    void tick(uint32_t now_ms) override;

    comms::ConfigService& config() { return config_; }
    const timing::DurableWriteWindow& durable_writes() const { return writes_; }

    // Changes core/power refused to write, counted once each and not once per
    // pass: one number a bench can read as "a pilot's change is waiting for a
    // charger". Not a fault of the placement policy above - it means something
    // else entirely, which is why it is not one of its counters.
    uint32_t refused_writes() const { return refused_; }
    bool holding_for_power() const { return held_; }

    // The one caller that may skip the WINDOW - not the power rule above, which
    // no caller skips: the shutdown sequencer, once the radio has been aborted.
    // The bound above answers the cell that dies without warning; this answers
    // the power-off that does give warning, because losing a change a pilot made
    // to a deliberate shutdown would be the same broken promise for no reason at
    // all.
    void flush_settings(uint32_t now_ms);

   private:
    static constexpr size_t kBlobCap = 64;

    void load();
    void drain_link_events();
    void take_request(uint32_t now_ms);
    void drain_settings(uint32_t now_ms);
    void persist();
    void confirm_image_once_healthy();

    // Whether the blob may go to flash at all, asked of the one service that
    // knows what the cell is doing. A reference, not a pointer: the product wires
    // it at construction and there is no version of this device where the
    // question has no owner.
    bool may_persist() const { return power_.may_write(power::DurableWrite::Settings); }
    bool hold_for_power();

    comms::ConfigService config_;
    const PowerService& power_;
    timing::DurableWriteWindow writes_{};
    uint32_t refused_{0};
    bool held_{false};
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
