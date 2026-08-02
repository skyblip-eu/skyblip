#include "core/comms/config.h"

#include <cstring>

#include "core/util/json_min.h"
#include "core/util/span.h"

namespace skyblip::comms {

void ConfigService::tick(uint32_t now_ms) {
    now_ms_ = now_ms;
    if (upload_window_open_ && now_ms - window_opened_ms_ >= kUploadWindowMs) {
        upload_window_open_ = false;
    }
}

void ConfigService::set_flight_state(FlightState fs) {
    if (fs == FlightState::Airborne) {
        airborne_latched_ = true;
        upload_window_open_ = false;
    }
    if (fs == FlightState::Ground) airborne_latched_ = false;
    if (airborne_latched_)
        flight_ = FlightState::Airborne;
    else if (fs == FlightState::Ground)
        flight_ = FlightState::Ground;
    else
        flight_ = FlightState::Unknown;
}

void ConfigService::on_link_up(const messages::LinkUp& up) { session_ = up.session_id; }

void ConfigService::on_link_down(const messages::LinkDown&) {
    pending_ = Pending::None;
    pending_len_ = 0;
    upload_window_open_ = false;
}

void ConfigService::reply(const char* json) {
    link_.send(messages::Endpoint::Config, ConstByteSpan(reinterpret_cast<const uint8_t*>(json),
                                                         static_cast<size_t>(std::strlen(json))));
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

void ConfigService::send_status() {
    char buf[192];
    json::Writer w(buf, sizeof(buf));
    w.kv_str("cmd", "status");
    w.kv_int("addr", static_cast<long>(settings_.device_addr));
    w.kv_str("callsign", settings_.callsign);
    w.kv_str("reset", power::to_string(reset_reason_));
    w.kv_str("flight", flight_name(flight_));
    w.kv_bool("upload", upload_allowed());
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
        char buf[256];
        json::Writer w(buf, sizeof(buf));
        w.kv_str("cmd", "config");
        char body[256];
        settings::to_json(settings_, body, sizeof(body));
        w.kv_str("_settings", body);
        w.finish();
        reply(buf);
        return;
    }

    if (std::strcmp(cmd, "status") == 0) {
        send_status();
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
        pending_ = Pending::Set;
        char buf[64];
        json::Writer w(buf, sizeof(buf));
        w.kv_bool("ack", false);
        w.kv_bool("pending", true);
        w.kv_str("reason", "confirm");
        w.finish();
        reply(buf);
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
        pending_ = requested;
        char buf[64];
        json::Writer w(buf, sizeof(buf));
        w.kv_bool("ack", false);
        w.kv_bool("pending", true);
        w.kv_str("reason", reason);
        w.finish();
        reply(buf);
        return;
    }

    ack(false, "unknown_cmd");
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
    }
}

void ConfigService::cancel() {
    pending_ = Pending::None;
    pending_len_ = 0;
    upload_window_open_ = false;
    ack(false, "cancelled");
}

}
