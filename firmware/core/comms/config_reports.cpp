#include "core/comms/config.h"
#include "core/comms/timing_report.h"
#include "core/util/json_min.h"

namespace skyblip::comms {

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
    w.kv_str("reset", power::to_string(diag_.reset));
    w.kv_str("flight", flight_name(flight_));
    w.kv_bool("upload", upload_allowed());
    w.kv_int("battery_percent", static_cast<long>(diag_.battery.percent));
    w.kv_bool("battery_valid", diag_.battery.valid);
    w.kv_bool("charging", diag_.battery.charging);
    w.kv_str("power_level", power::to_string(diag_.level));
    // Last, and only when a reading exists. Whole degrees, rounded by the one
    // rule the dump uses too (core/comms/diagnostics.h): a support case must not
    // read a different temperature depending on which surface answered it.
    if (diag_.die_valid) w.kv_int("die_temp_c", whole_celsius(diag_.die_decicelsius));
    const int len = w.finish();
    if (w.overflowed()) {
        diag_.link_drops++;
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
        diag_.link_drops++;
        return;
    }
    char buf[kTimingFrameCap];
    while (!report.exhausted()) {
        const int len = report.next_frame(payload(), buf, static_cast<int>(sizeof(buf)));
        if (len <= 0) {
            diag_.link_drops++;
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
    w.kv_bool("writable", settings_writable());
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

// The radio's half of the same bench, still its own question rather than four
// more keys on "status": that reply is the one this service PUSHES unsolicited
// and it is already sized against the narrowest phone in the field at its worst
// case. A status push that vanished whenever a unit was hot AND refusing packets
// would be the exact failure the payload ceiling exists to prevent.
void ConfigService::send_radio() {
    DiagnosticsReport report(diag_, "radio", DiagnosticsReport::Group::Radio);
    send_report(report);
}

void ConfigService::send_update() {
    char from[dfu::kVersionTextCap];
    char to[dfu::kVersionTextCap];
    dfu::format_version(update_record_.from, from, sizeof(from));
    dfu::format_version(update_record_.to, to, sizeof(to));
    char buf[128];
    json::Writer w(buf, sizeof(buf));
    w.kv_str("cmd", "update");
    w.kv_str("image", dfu::to_string(image_state_));
    if (image_state_ != dfu::ImageState::Confirmed) {
        w.kv_str("from", from);
        w.kv_str("to", to);
    }
    w.kv_bool("swap_powered", swap_powered());
    w.finish();
    reply(buf);
}

// A whole dump nobody has collected is a dump of zeros, and zeros here read as a
// receiver that has never seen a satellite and a supply that has never faltered -
// which is a device reporting health it has not measured. So it is refused with a
// reason, exactly as an unwired timing accumulator is. The radio group above is not
// gated: it predates the collector, its own key is written every pass by whoever
// owns the table (set_range_refused), and a radio that has heard nothing truthfully
// has zero receptions.
void ConfigService::send_diagnostics() {
    if (diag_.refreshes == 0) {
        ack(false, "no_stats");
        return;
    }
    DiagnosticsReport report(diag_, "diag");
    send_report(report);
}

void ConfigService::send_report(DiagnosticsReport& report) {
    if (!report.fits(payload())) {
        diag_.link_drops++;
        return;
    }
    char buf[DiagnosticsReport::kFrameCap];
    while (!report.exhausted()) {
        const int len = report.next_frame(payload(), buf, static_cast<int>(sizeof(buf)));
        if (len <= 0) {
            diag_.link_drops++;
            return;
        }
        if (reply(buf, len) != Status::Ok) return;
    }
}

}  // namespace skyblip::comms
