#include "products/skyblip_go/services/config.h"

namespace skyblip::go {

Status ConfigLinkService::setup() {
    load();
    context_.state.own.aircraft_cat = context_.state.settings.aircraft_type;
    return Status::Ok;
}

void ConfigLinkService::tick(uint32_t now_ms) {
    // INFO: cf 02aug26 The gate's one source of truth. core/flight owns the
    // decision and publishes the ADS-L G.1.4 code on the bus; this reads it
    // there rather than keeping a second opinion, so "on the ground" means the
    // same thing to the update lockout as it does to the transmitter. Nobody
    // calling this is what left flight_ at Unknown, which refuses everything.
    config_.set_flight_state(comms::flight_state_from(context_.state.own.flight_state));
    // The radio service is the only writer of the threshold in force, exactly as
    // core/flight is the only writer of the gate above: read it there.
    config_.set_carrier_sense(context_.state.carrier_sense_dbm);

    // INFO: cf 02aug26 core/power decided what the divider reading means and
    // what a low cell is; this hands the already-decided numbers to the link
    // rather than keeping a second opinion, same as the flight gate above it.
    config_.set_battery_state(context_.state.battery, context_.state.power_level);

    messages::RxFrame frame{};
    while (context_.bus.link_rx.pop(frame)) config_.on_rx(frame);

    config_.tick(now_ms);
    if (config_.settings_dirty()) {
        persist();
        config_.clear_dirty();
    }
    confirm_image_once_healthy();
}

void ConfigLinkService::load() {
    context_.state.settings = settings::defaults(context_.roles.device_addr);
    if (!hal::has(context_.roles.capabilities, hal::Capability::Storage)) return;

    uint8_t blob[kBlobCap];
    size_t n = 0;
    if (!is_ok(context_.roles.kv.read("settings", blob, sizeof(blob), n))) return;
    settings::Settings loaded;
    if (is_ok(settings::from_blob(blob, n, loaded)) && is_ok(settings::validate(loaded)))
        context_.state.settings = loaded;
}

void ConfigLinkService::persist() {
    if (!hal::has(context_.roles.capabilities, hal::Capability::Storage)) return;
    uint8_t blob[kBlobCap];
    settings::to_blob(context_.state.settings, blob, sizeof(blob));
    context_.roles.kv.write("settings", blob, settings::blob_size());
}

// A fresh image swapped in by MCUboot is on probation: unless it declares itself
// good, the bootloader restores the previous one on the next boot. So "good" must
// mean more than "main() ran": wait until the radio is up AND a GNSS fix has
// arrived, which between them exercises SPI, the SX1262, the UART and core/gnss.
void ConfigLinkService::confirm_image_once_healthy() {
    if (image_confirmed_ || !hal::has(context_.roles.capabilities, hal::Capability::Dfu)) return;
    if (!context_.state.started || context_.state.gnss_fixes == 0) return;
    context_.roles.dfu.confirm();
    image_confirmed_ = true;
}

}  // namespace skyblip::go
