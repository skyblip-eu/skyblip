#include "core/comms/config.h"

#include <cstring>

#include "core/util/format.h"
#include "core/util/json_min.h"
#include "core/util/span.h"

namespace skyblip::comms {

namespace {
// Seven counts, comma-joined: cheaper than a JSON array over a FLAT-JSON
// writer that does not have one, and just as readable on a bench terminal.
// A bench run collects one PPS edge a second for hours, so a bucket reaches
// seven figures and the text has to have room for the widest count there is:
// a histogram that silently loses its last bucket is not evidence.
constexpr int kBucketsTextCap = timing::SlotTimingStats::kBuckets * 11 + 1;
int format_buckets(const timing::SlotTimingStats& stats, bool pps, char* out, int cap) {
    int n = 0;
    for (int i = 0; i < timing::SlotTimingStats::kBuckets && n < cap - 1; i++) {
        if (i > 0) out[n++] = ',';
        n += fmt_uint(out + n, pps ? stats.pps_bucket(i) : stats.dwell_bucket(i));
    }
    out[n] = 0;
    return n;
}
}  // namespace

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

void ConfigService::reply(const char* json) {
    link_.send(messages::Endpoint::Config, ConstByteSpan(reinterpret_cast<const uint8_t*>(json),
                                                         static_cast<size_t>(std::strlen(json))));
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

void ConfigService::send_status() {
    char buf[256];
    json::Writer w(buf, sizeof(buf));
    w.kv_str("cmd", "status");
    w.kv_int("addr", static_cast<long>(settings_.device_addr));
    w.kv_str("callsign", settings_.callsign);
    w.kv_str("reset", power::to_string(reset_reason_));
    w.kv_str("flight", flight_name(flight_));
    w.kv_bool("upload", upload_allowed());
    w.kv_int("battery_percent", static_cast<long>(battery_.percent));
    w.kv_bool("battery_valid", battery_.valid);
    w.kv_bool("charging", battery_.charging);
    w.kv_str("power_level", power::to_string(power_level_));
    w.finish();
    reply(buf);
}

// pps_us and dwell_us are the two histograms in kBuckets order: the SX1262's
// own retune time, core/timing::kHopGuardMs and core/timing::kJitterGuardMs on
// each side of the centre bucket (core/timing/timing_stats.h). holdover,
// missed and refused are counted apart from both, on purpose: a fault a
// histogram cannot bound must not be folded into one that can.
void ConfigService::send_timing() {
    if (timing_stats_ == nullptr) {
        ack(false, "no_stats");
        return;
    }
    char pps[kBucketsTextCap];
    char dwell[kBucketsTextCap];
    format_buckets(*timing_stats_, true, pps, sizeof(pps));
    format_buckets(*timing_stats_, false, dwell, sizeof(dwell));

    char buf[512];
    json::Writer w(buf, sizeof(buf));
    w.kv_str("cmd", "timing");
    w.kv_str("pps_us", pps);
    w.kv_int("pps_worst_us", timing_stats_->pps_worst_us());
    w.kv_int("pps_samples", static_cast<long>(timing_stats_->pps_samples()));
    w.kv_str("dwell_us", dwell);
    w.kv_int("dwell_worst_us", timing_stats_->dwell_worst_us());
    w.kv_int("dwell_samples", static_cast<long>(timing_stats_->dwell_samples()));
    w.kv_int("holdover", static_cast<long>(timing_stats_->holdover_events()));
    w.kv_int("missed", static_cast<long>(timing_stats_->missed()));
    w.kv_int("refused", static_cast<long>(timing_stats_->refused()));
    w.kv_int("carrier_sense_dbm", carrier_sense_dbm_);
    w.kv_int("carrier_sense_ceiling_dbm", timing::NoiseFloor::kThresholdCeilingDbm);
    w.kv_int("carrier_sense_us", static_cast<long>(timing::CarrierSense::kAssessmentUs));
    w.finish();
    reply(buf);
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
