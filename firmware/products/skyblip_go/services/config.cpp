#include "products/skyblip_go/services/config.h"

#include <cstring>

namespace skyblip::go {

Status ConfigLinkService::setup() {
    load();
    context_.state.own.aircraft_cat = context_.state.settings.aircraft_type;
    return Status::Ok;
}

void ConfigLinkService::tick(uint32_t now_ms) {
    drain_link_events();
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
    // And the half no voltage can report: the SoC's own power-failure comparator
    // has fired. It is what turns a "set" from a phone into a refusal at the
    // door, so it travels the same way the level does - decided in core/power,
    // carried here, never re-derived.
    config_.set_supply_warned(power_.supply_warned());
    // Read where it is already sampled, on the cadence the sensor deserves: the
    // service that owns the cell owns the die too, and this only carries the
    // number to the link.
    config_.set_die_temperature(power_.die_temperature_dc(), power_.die_temperature_valid());
    // The range gate's own counter, read off the table that keeps it rather than
    // recounted here (core/traffic/table.h).
    config_.set_range_refused(context_.state.traffic.implausible_count());

    messages::RxFrame frame{};
    while (context_.bus.link_rx.pop(frame)) config_.on_rx(frame);

    config_.tick(now_ms);
    drain_settings(now_ms);
    confirm_image_once_healthy();
}

// INFO: le 04aug26 The single reader of the connection, drained once per pass and
// first, because everything below it is said about a link: the standing prompt a
// disconnect cancels, the upload window it closes, and the gauge that only
// pushes while somebody is listening. Nothing published these events until the
// board raised them from the platform; the host suite was green because every
// case called on_link_up() by hand.
void ConfigLinkService::drain_link_events() {
    messages::LinkEvent event{};
    while (context_.bus.link_events.pop(event)) {
        if (event.type == messages::LinkEventType::Up)
            config_.on_link_up(messages::LinkUp{event.session_id, event.payload_bytes});
        else
            config_.on_link_down(messages::LinkDown{event.session_id});
    }
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

// Asked BEFORE the request is taken, so a refusal leaves the change dirty rather
// than pending: a pending change the policy cannot place is one the bound would
// eventually force onto flash, which is exactly the write this refuses. Held
// here, it survives until the cell does or does not come back. Counted once per
// change rather than once per pass, so the number reads as changes waiting.
bool ConfigLinkService::hold_for_power() {
    if (may_persist()) {
        held_ = false;
        return false;
    }
    if (!held_ && (config_.settings_dirty() || writes_.pending())) {
        held_ = true;
        refused_++;
    }
    return true;
}

void ConfigLinkService::drain_settings(uint32_t now_ms) {
    if (hold_for_power()) return;
    take_request(now_ms);
    const timing::DurableWriteVerdict verdict =
        writes_.decide(context_.state.plan, context_.state.dwell, now_ms);
    if (verdict != timing::DurableWriteVerdict::Place &&
        verdict != timing::DurableWriteVerdict::Forced)
        return;
    persist();
    writes_.placed(now_ms, verdict == timing::DurableWriteVerdict::Forced);
}

// The deliberate power-off. Same gate: a cell that is collapsing takes the log
// record with it and leaves the settings sector alone, which is the whole of
// core/power's rule. A LowBattery shutdown is therefore the one power-off that
// does not flush, and that is the trade written down in the header - the change a
// pilot was making, against every change they ever made.
void ConfigLinkService::flush_settings(uint32_t now_ms) {
    if (hold_for_power()) return;
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
