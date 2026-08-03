#include "products/skyblip_go/services/config.h"

#include <cstring>

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
    drain_settings(now_ms);
    confirm_image_once_healthy();
}

// The whole of the deferral, and it is short because the policy owns the
// arithmetic: a request in, a verdict out, and one write when the verdict says so.
// Taken the moment it is raised, and never held: the blob is read at the instant
// it is written, so a stream of changes arrives here as a stream of requests and
// leaves as one write.
void ConfigLinkService::take_request(uint32_t now_ms) {
    if (!config_.settings_dirty()) return;
    config_.clear_dirty();
    writes_.request(now_ms);
}

void ConfigLinkService::drain_settings(uint32_t now_ms) {
    take_request(now_ms);
    const timing::DurableWriteVerdict verdict =
        writes_.decide(context_.state.plan, context_.state.dwell, now_ms);
    if (verdict != timing::DurableWriteVerdict::Place &&
        verdict != timing::DurableWriteVerdict::Forced)
        return;
    persist();
    writes_.placed(now_ms, verdict == timing::DurableWriteVerdict::Forced);
}

void ConfigLinkService::flush_settings(uint32_t now_ms) {
    take_request(now_ms);
    if (!writes_.pending()) return;
    persist();
    writes_.placed(now_ms, /*forced=*/false);
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
    if (n <= kBlobCap) {
        std::memcpy(stored_, blob, n);
        stored_len_ = n;
    }
}

void ConfigLinkService::persist() {
    if (!hal::has(context_.roles.capabilities, hal::Capability::Storage)) return;
    uint8_t blob[kBlobCap];
    settings::to_blob(context_.state.settings, blob, sizeof(blob));
    const size_t len = settings::blob_size();
    if (stored_len_ == len && std::memcmp(stored_, blob, len) == 0) return;
    if (!is_ok(context_.roles.kv.write("settings", blob, len))) return;
    std::memcpy(stored_, blob, len);
    stored_len_ = len;
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
