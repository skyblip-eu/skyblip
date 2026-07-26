#include "core/comms/config.h"

#include <cstring>

#include "core/util/json_min.h"
#include "core/util/span.h"

namespace skyblip::comms {

void ConfigService::set_flight_state(FlightState fs) {
    if (fs == FlightState::Airborne) airborne_latched_ = true;
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

    if (std::strcmp(cmd, "dfu") == 0) {
        if (!on_ground()) {
            ack(false, "in_flight");
            return;
        }
        pending_ = Pending::Dfu;
        char buf[64];
        json::Writer w(buf, sizeof(buf));
        w.kv_bool("ack", false);
        w.kv_bool("pending", true);
        w.kv_str("reason", "confirm_dfu");
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
        ack(true, "dfu");
        if (dfu_) dfu_->trigger();
    }
}

void ConfigService::cancel() {
    pending_ = Pending::None;
    pending_len_ = 0;
    ack(false, "cancelled");
}

}
