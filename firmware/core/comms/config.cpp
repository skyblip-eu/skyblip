#include "core/comms/config.h"

#include <cstring>

#include "core/comms/timing_report.h"
#include "core/util/json_min.h"
#include "core/util/span.h"

namespace skyblip::comms {

FlightState flight_state_from(uint8_t adsl_code) {
    switch (static_cast<flight::FlightState>(adsl_code)) {
        case flight::FlightState::OnGround: return FlightState::Ground;
        case flight::FlightState::Airborne: return FlightState::Airborne;
        case flight::FlightState::Unknown: break;
    }
    return FlightState::Unknown;
}

const char* pending_title(Pending pending) {
    switch (pending) {
        case Pending::Set: return "SETTINGS";
        case Pending::Dfu: return "FIRMWARE";
        case Pending::Apply: return "INSTALL";
        case Pending::Recovery: return "RECOVERY";
        case Pending::PowerOff: return "POWER OFF";
        case Pending::EraseLog: return "ERASE LOGS";
        case Pending::None: break;
    }
    return "";
}

const char* pending_detail(Pending pending) {
    switch (pending) {
        case Pending::Set: return "APPLY THE SETTINGS THE PHONE SENT";
        case Pending::Dfu: return "LET THE PHONE WRITE A NEW IMAGE";
        case Pending::Apply: return "REBOOT INTO THE STAGED IMAGE";
        case Pending::Recovery: return "REBOOT INTO THE USB BOOTLOADER";
        case Pending::PowerOff: return "SHUT THE DEVICE DOWN";
        case Pending::EraseLog: return "DELETE EVERY FLIGHT ON THE DEVICE";
        case Pending::None: break;
    }
    return "";
}

void ConfigService::tick(uint32_t now_ms) {
    now_ms_ = now_ms;
    // INFO: fc 04aug26 The one frame nobody asked for is the one nobody will ask
    // for again, so a controller that was momentarily out of buffers gets another
    // pass rather than costing a pilot a stale gauge. Only a transient refusal
    // arms this: a frame too big for the payload would retry forever.
    if (status_push_due_ && link_up_) send_status();
    if (upload_window_open_ && now_ms - window_opened_ms_ >= kUploadWindowMs) {
        upload_window_open_ = false;
    }
    if (pending_ != Pending::None && now_ms - pending_since_ms_ >= kConfirmWindowMs) {
        pending_ = Pending::None;
        pending_len_ = 0;
        ack(false, "expired");
    }
}

void ConfigService::set_flight_state(FlightState fs) {
    if (fs == FlightState::Airborne) {
        airborne_latched_ = true;
        upload_window_open_ = false;
        // A prompt that survived the take-off roll would be an authorisation a
        // pilot could confirm with an elbow in the air.
        if (pending_ != Pending::None) {
            pending_ = Pending::None;
            pending_len_ = 0;
            ack(false, "in_flight");
        }
    }
    if (fs == FlightState::Ground) airborne_latched_ = false;
    if (airborne_latched_)
        flight_ = FlightState::Airborne;
    else if (fs == FlightState::Ground)
        flight_ = FlightState::Ground;
    else
        flight_ = FlightState::Unknown;
}

void ConfigService::on_link_up(const messages::LinkUp& up) {
    session_ = up.session_id;
    link_up_ = true;
}

void ConfigService::on_link_down(const messages::LinkDown&) {
    pending_ = Pending::None;
    pending_len_ = 0;
    upload_window_open_ = false;
    link_up_ = false;
    status_push_due_ = false;
}

namespace {
uint8_t battery_step(uint8_t percent) {
    return static_cast<uint8_t>(percent / kBatteryPushStepPercent);
}
}

void ConfigService::set_battery_state(const power::BatteryState& battery, power::PowerLevel level) {
    const bool charging_changed = battery.charging != battery_.charging;
    const bool level_changed = level != power_level_;
    const bool step_changed = battery_step(battery.percent) != battery_step(battery_.percent);

    battery_ = battery;
    power_level_ = level;

    if (link_up_ && (charging_changed || level_changed || step_changed)) send_status();
}

int ConfigService::payload() const { return static_cast<int>(link_.payload_bytes()); }

// INFO: fc 04aug26 The only door to the link, and it refuses out loud. A frame
// longer than the negotiated payload is not shortened by the controller, it
// fails, so it is counted here and never handed down. Everything that comes
// through this door is a request/response answer except the status push, so the
// recovery from a lost one is the phone's next command; the push has tick().
Status ConfigService::reply(const char* json, int len) {
    if (len <= 0 || len > payload()) {
        link_drops_++;
        return Status::OutOfRange;
    }
    const Status sent =
        link_.send(messages::Endpoint::Config,
                   ConstByteSpan(reinterpret_cast<const uint8_t*>(json), static_cast<size_t>(len)));
    if (!is_ok(sent)) link_drops_++;
    return sent;
}

Status ConfigService::reply(const char* json) {
    return reply(json, static_cast<int>(std::strlen(json)));
}

void ConfigService::stage(Pending pending, const char* reason) {
    pending_ = pending;
    pending_since_ms_ = now_ms_;
    char buf[64];
    json::Writer w(buf, sizeof(buf));
    w.kv_bool("ack", false);
    w.kv_bool("pending", true);
    w.kv_str("reason", reason);
    w.finish();
    reply(buf);
}

void ConfigService::ack(bool ok, const char* reason) {
    char buf[96];
    json::Writer w(buf, sizeof(buf));
    w.kv_bool("ack", ok);
    if (reason) w.kv_str("reason", reason);
    w.finish();
    reply(buf);
}

const char* ConfigService::flight_name(FlightState fs) {
    switch (fs) {
        case FlightState::Ground: return "ground";
        case FlightState::Airborne: return "airborne";
        case FlightState::Unknown: break;
    }
    return "unknown";
}

// INFO: fc 04aug26 Sized to fit the narrowest phone in the field, at its worst
// case, by carrying state and nothing else: the device address and the callsign
// left this frame for the "config" reply that already answers them, because
// duplicating identity in the one frame that gets pushed unsolicited is what put
// it over an iPhone's 182 bytes. The buffer is the limit itself, so a field added
// later cannot quietly overflow it - the writer leaves the field out whole and
// overflowed() refuses the frame instead.
void ConfigService::send_status() {
    char buf[kSmallestSupportedPayload + 1];
    json::Writer w(buf, sizeof(buf));
    w.kv_str("cmd", "status");
    w.kv_str("reset", power::to_string(reset_reason_));
    w.kv_str("flight", flight_name(flight_));
    w.kv_bool("upload", upload_allowed());
    w.kv_int("battery_percent", static_cast<long>(battery_.percent));
    w.kv_bool("battery_valid", battery_.valid);
    w.kv_bool("charging", battery_.charging);
    w.kv_str("power_level", power::to_string(power_level_));
    const int len = w.finish();
    if (w.overflowed()) {
        link_drops_++;
        status_push_due_ = false;
        return;
    }
    status_push_due_ = reply(buf, len) == Status::WouldBlock;
}

// INFO: fc 04aug26 The one sender allowed more than one frame, and the reason
// lives in core/comms/timing_report.h: a laboratory reads this once, so a
// two-frame answer is free, while trimming a histogram would destroy the
// evidence it was asked for. A frame that cannot be produced at all is counted
// and the rest of the report is abandoned rather than sent with a hole in it.
void ConfigService::send_timing() {
    if (timing_stats_ == nullptr) {
        ack(false, "no_stats");
        return;
    }
    TimingReport report(*timing_stats_, carrier_sense_dbm_);
    if (!report.fits(payload())) {
        link_drops_++;
        return;
    }
    char buf[kTimingFrameCap];
    while (!report.exhausted()) {
        const int len = report.next_frame(payload(), buf, static_cast<int>(sizeof(buf)));
        if (len <= 0) {
            link_drops_++;
            return;
        }
        if (reply(buf, len) != Status::Ok) return;
    }
}

// The durable-write half of the same bench: the settings writes the device made,
// the changes they were coalesced from, and the ones the policy could not place
// inside a free phase of the second. `forced` is the only number here that is a
// fault, and it is the reason this is exported at all - a stall that landed where
// the slot map did not budget for one must not be silent.
//
// Deliberately its own object rather than four more keys on "timing": that reply
// is already the longest thing this service sends, and the link negotiates a
// notification size it has to fit inside. Same endpoint, same dispatch, one more
// question - not a longer answer a phone might truncate.
void ConfigService::send_flash() {
    if (writes_ == nullptr) {
        ack(false, "no_stats");
        return;
    }
    char buf[192];
    json::Writer w(buf, sizeof(buf));
    w.kv_str("cmd", "flash");
    w.kv_int("changes", static_cast<long>(writes_->requests()));
    w.kv_int("writes", static_cast<long>(writes_->writes()));
    w.kv_int("forced", static_cast<long>(writes_->forced()));
    w.kv_int("worst_wait_ms", static_cast<long>(writes_->worst_wait_ms()));
    w.kv_bool("pending", writes_->pending());
    w.kv_int("budget_ms", static_cast<long>(timing::DurableWriteWindow::kWorstWriteMs));
    w.kv_int("bound_ms", static_cast<long>(timing::DurableWriteWindow::kMaxDeferMs));
    w.finish();
    reply(buf);
}

void ConfigService::on_rx(const messages::RxFrame& frame) {
    if (frame.endpoint != messages::Endpoint::Config) return;
    const char* data = reinterpret_cast<const char*>(frame.data.data());
    int len = frame.len;
    json::Reader r(data, len);
    char cmd[16] = {0};
    if (!r.get_str("cmd", cmd, sizeof(cmd))) {
        ack(false, "no_cmd");
        return;
    }

    if (std::strcmp(cmd, "get") == 0) {
        // INFO: fc 04aug26 One flat object, which is also what
        // schemas/config.v1.schema.json describes: the settings used to travel
        // nested as an escaped string, and the backslashes alone were 53 bytes
        // on a 158-byte body. Sized by its buffer at the smallest payload we
        // support, and refused rather than answered short - an app that receives
        // a config object with fields missing renders defaults a pilot never set.
        char buf[kSmallestSupportedPayload + 1];
        json::Writer w(buf, sizeof(buf));
        w.kv_str("cmd", "config");
        settings::write_json_fields(w, settings_);
        const int reply_len = w.finish();
        if (w.overflowed()) {
            link_drops_++;
            return;
        }
        reply(buf, reply_len);
        return;
    }

    if (std::strcmp(cmd, "status") == 0) {
        send_status();
        return;
    }

    if (std::strcmp(cmd, "timing") == 0) {
        send_timing();
        return;
    }

    if (std::strcmp(cmd, "flash") == 0) {
        send_flash();
        return;
    }

    if (std::strcmp(cmd, "set") == 0) {
        if (!on_ground()) {
            ack(false, "in_flight");
            return;
        }
        pending_len_ = len < static_cast<int>(sizeof(pending_buf_)) - 1
                           ? len
                           : static_cast<int>(sizeof(pending_buf_)) - 1;
        std::memcpy(pending_buf_, data, static_cast<size_t>(pending_len_));
        pending_buf_[pending_len_] = 0;
        stage(Pending::Set, "confirm");
        return;
    }

    // "dfu" opens an upload window. The image itself then travels over MCUmgr
    // /SMP, not over this channel. "apply" swaps an image already staged in the
    // secondary slot. "recovery" reboots into the factory drag-and-drop
    // bootloader. "power_off" asks the shutdown sequencer for the rails. All
    // four take the same route: refuse in flight, then wait for a button press.
    Pending requested = Pending::None;
    const char* reason = nullptr;
    if (std::strcmp(cmd, "dfu") == 0) {
        requested = Pending::Dfu;
        reason = "confirm_dfu";
    } else if (std::strcmp(cmd, "apply") == 0) {
        requested = Pending::Apply;
        reason = "confirm_apply";
    } else if (std::strcmp(cmd, "recovery") == 0) {
        requested = Pending::Recovery;
        reason = "confirm_recovery";
    } else if (std::strcmp(cmd, "power_off") == 0) {
        requested = Pending::PowerOff;
        reason = "confirm_power_off";
    }

    if (requested != Pending::None) {
        if (!on_ground()) {
            ack(false, "in_flight");
            return;
        }
        stage(requested, reason);
        return;
    }

    ack(false, "unknown_cmd");
}

void ConfigService::request_log_erase() {
    if (!on_ground()) {
        ack(false, "in_flight");
        return;
    }
    stage(Pending::EraseLog, "confirm_erase_log");
}

void ConfigService::confirm() {
    if (!on_ground()) {
        pending_ = Pending::None;
        pending_len_ = 0;
        ack(false, "in_flight");
        return;
    }
    if (pending_ == Pending::Set) {
        Status st = settings::apply_json(settings_, pending_buf_, pending_len_);
        pending_ = Pending::None;
        pending_len_ = 0;
        if (st == Status::Ok) {
            dirty_ = true;
            ack(true, nullptr);
        } else {
            ack(false, to_string(st));
        }
    } else if (pending_ == Pending::Dfu) {
        pending_ = Pending::None;
        upload_window_open_ = true;
        window_opened_ms_ = now_ms_;
        ack(true, "dfu");
    } else if (pending_ == Pending::Apply) {
        pending_ = Pending::None;
        upload_window_open_ = false;
        ack(true, "apply");
        if (dfu_) dfu_->trigger();
    } else if (pending_ == Pending::Recovery) {
        pending_ = Pending::None;
        upload_window_open_ = false;
        ack(true, "recovery");
        if (dfu_) dfu_->enter_recovery();
    } else if (pending_ == Pending::PowerOff) {
        pending_ = Pending::None;
        upload_window_open_ = false;
        power_off_requested_ = true;
        ack(true, "power_off");
    } else if (pending_ == Pending::EraseLog) {
        pending_ = Pending::None;
        log_erase_requested_ = true;
        ack(true, "erase_log");
    }
}

void ConfigService::cancel() {
    pending_ = Pending::None;
    pending_len_ = 0;
    upload_window_open_ = false;
    ack(false, "cancelled");
}

}
