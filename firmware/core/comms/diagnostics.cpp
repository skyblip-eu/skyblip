#include "core/comms/diagnostics.h"

#include "core/comms/frame_budget.h"
#include "core/util/json_min.h"

namespace skyblip::comms {

namespace {

int write_text(char* out, const char* s) {
    int n = 0;
    while (s[n] != 0) {
        out[n] = s[n];
        n++;
    }
    return n;
}

int write_long(char* out, long v) {
    const int n = frame::number_bytes(v);
    unsigned long magnitude =
        v < 0 ? static_cast<unsigned long>(-v) : static_cast<unsigned long>(v);
    int at = n;
    do {
        out[--at] = static_cast<char>('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude != 0);
    if (v < 0) out[0] = '-';
    return n;
}

const char* bool_text(bool v) { return v ? "true" : "false"; }

// The saturating counter both link reports share, and why they have to
// (core/comms/frame_budget.h).
using frame::counter;

}  // namespace

const char* reject_name(gnss::FixReject reason) {
    switch (reason) {
        case gnss::FixReject::NoSolution: return "NO SOLUTION";
        case gnss::FixReject::MissingRmc: return "NO RMC";
        case gnss::FixReject::MissingGga: return "NO GGA";
        case gnss::FixReject::Stale: return "STALE";
        case gnss::FixReject::NoDate: return "NO DATE";
        case gnss::FixReject::Jump: return "JUMP";
        case gnss::FixReject::None: break;
    }
    return "NONE";
}

int whole_celsius(int16_t decicelsius) {
    const int16_t bias = decicelsius >= 0 ? 5 : -5;
    return (decicelsius + bias) / 10;
}

DiagnosticsReport::DiagnosticsReport(const Diagnostics& diagnostics, const char* cmd) : cmd_(cmd) {
    build(diagnostics, nullptr);
}

DiagnosticsReport::DiagnosticsReport(const Diagnostics& diagnostics, const char* cmd, Group only)
    : cmd_(cmd) {
    build(diagnostics, &only);
}

// THE TABLE. Both surfaces are this list, in this order, so a field added here
// reaches the console and the link in one edit and cannot reach one of them under
// a different name. Every value is a plain read of something the device already
// keeps: nothing here computes, samples or clears anything.
void DiagnosticsReport::build(const Diagnostics& d, const Group* only) {
    const bool sys = only == nullptr || *only == Group::Sys;
    const bool radio = only == nullptr || *only == Group::Radio;
    const bool traffic = only == nullptr || *only == Group::Traffic;
    const bool gnss = only == nullptr || *only == Group::Gnss;
    const bool power = only == nullptr || *only == Group::Power;

    if (sys) {
        add_int(Group::Sys, "up_s", counter(d.uptime_s));
        add_text(Group::Sys, "reset", power::to_string(d.reset));
        add_int(Group::Sys, "link_drops", counter(d.link_drops));
    }

    if (radio) {
        add_int(Group::Radio, "noise_dbm", d.noise_dbm);
        add_int(Group::Radio, "lbt_dbm", d.lbt_dbm);
        add_int(Group::Radio, "duty_permille", counter(d.duty_permille));
        add_int(Group::Radio, "gave_up", counter(d.gave_up));
        add_int(Group::Radio, "rx_ok", counter(d.rx_ok));
        add_int(Group::Radio, "rx_bad", counter(d.rx_bad));
        add_int(Group::Radio, "tx_ok", counter(d.tx_ok));
        add_int(Group::Radio, "tx_busy", counter(d.tx_busy));
        // The range gate's refusals read out with the radio and not with the
        // table, because what they measure is this receiver's link budget: a rate
        // that climbs is a decoder correcting a frame into a position no radio
        // this size could have heard (core/traffic/sanity.h).
        add_int(Group::Radio, "range_refused", counter(d.range_refused));
    }

    if (traffic) {
        add_int(Group::Traffic, "tracked", counter(d.tracked));
        add_int(Group::Traffic, "alarm", d.alarm);
    }

    if (gnss) {
        add_int(Group::Gnss, "fixes", counter(d.gnss_fixes));
        add_bool(Group::Gnss, "valid", d.fix_valid);
        // A receiver silently at another rate is a GNSS-less device that looks
        // fitted, and it is the top support case this part will generate.
        add_int(Group::Gnss, "baud", counter(d.gnss_baud));
        add_bool(Group::Gnss, "identified", d.gnss_identified);
        add_text(Group::Gnss, "firmware", d.gnss_firmware);
        add_text(Group::Gnss, "reject", reject_name(d.gnss_reject));
        add_int(Group::Gnss, "rejected", counter(d.gnss_rejected));
        // Absent rather than zero: a device with no two consecutive fixes has no
        // residual, and 0 m is what a perfect one reports.
        if (d.resid_valid) add_int(Group::Gnss, "resid_m", d.resid_m);
    }

    if (power) {
        add_int(Group::Power, "mv", d.battery.millivolts);
        add_int(Group::Power, "percent", d.battery.percent);
        add_bool(Group::Power, "valid", d.battery.valid);
        add_bool(Group::Power, "charging", d.battery.charging);
        add_text(Group::Power, "level", power::to_string(d.level));
        add_int(Group::Power, "supply_warnings", counter(d.supply_warnings));
        add_int(Group::Power, "implausible", counter(d.battery_implausible));
        // Same rule as the status reply, and the same key: no reading is no key,
        // never a zero, because 0.0 C is a plausible hangar morning.
        if (d.die_valid) add_int(Group::Power, "die_temp_c", whole_celsius(d.die_decicelsius));
    }
}

void DiagnosticsReport::add_int(Group group, const char* key, long value) {
    if (count_ >= kMaxFields) return;
    fields_[count_++] = Field{key, nullptr, value, Kind::Int, group};
    if (count_ == 1 || fields_[count_ - 2].group != group) line_count_++;
}

void DiagnosticsReport::add_bool(Group group, const char* key, bool value) {
    if (count_ >= kMaxFields) return;
    fields_[count_++] = Field{key, nullptr, value ? 1 : 0, Kind::Bool, group};
    if (count_ == 1 || fields_[count_ - 2].group != group) line_count_++;
}

void DiagnosticsReport::add_text(Group group, const char* key, const char* value) {
    if (count_ >= kMaxFields) return;
    fields_[count_++] = Field{key, value, 0, Kind::Text, group};
    if (count_ == 1 || fields_[count_ - 2].group != group) line_count_++;
}

const char* DiagnosticsReport::group_name(Group group) {
    switch (group) {
        case Group::Sys: return "sys";
        case Group::Radio: return "radio";
        case Group::Traffic: return "traffic";
        case Group::Gnss: return "gnss";
        case Group::Power: return "power";
    }
    return "?";
}

int DiagnosticsReport::field_bytes(const Field& field) {
    switch (field.kind) {
        case Kind::Int: return frame::int_field_bytes(field.key, field.value);
        case Kind::Bool: return frame::bool_field_bytes(field.key);
        case Kind::Text: return frame::text_field_bytes(field.key, field.text);
    }
    return 0;
}

int DiagnosticsReport::line_start(int index) const {
    if (index < 0) return count_;
    int at = 0;
    for (int line = 0; at < count_; line++) {
        if (line == index) return at;
        const Group group = fields_[at].group;
        while (at < count_ && fields_[at].group == group) at++;
    }
    return count_;
}

int DiagnosticsReport::line(int index, char* buf, int cap) const {
    const int start = line_start(index);
    if (start >= count_) return 0;
    const Group group = fields_[start].group;

    int end = start;
    int need = frame::text_bytes(group_name(group));
    while (end < count_ && fields_[end].group == group) {
        const Field& field = fields_[end];
        // " key=" plus the value, which is quoted when it is text so a reset
        // reason with a space in it stays one field on a line and one value in the
        // JSON.
        need += 2 + frame::text_bytes(field.key);
        switch (field.kind) {
            case Kind::Int: need += frame::number_bytes(field.value); break;
            case Kind::Bool: need += frame::text_bytes(bool_text(field.value != 0)); break;
            case Kind::Text: need += frame::text_bytes(field.text) + 2; break;
        }
        end++;
    }
    if (need + 1 > cap) return 0;

    int n = write_text(buf, group_name(group));
    for (int i = start; i < end; i++) {
        const Field& field = fields_[i];
        buf[n++] = ' ';
        n += write_text(buf + n, field.key);
        buf[n++] = '=';
        switch (field.kind) {
            case Kind::Int: n += write_long(buf + n, field.value); break;
            case Kind::Bool: n += write_text(buf + n, bool_text(field.value != 0)); break;
            case Kind::Text:
                buf[n++] = '"';
                n += write_text(buf + n, field.text);
                buf[n++] = '"';
                break;
        }
    }
    buf[n] = 0;
    return n;
}

// Asked before the first frame goes out, so the answer is all or nothing: a bench
// holding four frames of a five-frame dump, with no fifth one coming, has been
// told a fault it cannot see is absent.
bool DiagnosticsReport::fits(int payload) const {
    int widest_field = 0;
    int widest_group = 0;
    for (int i = 0; i < count_; i++) {
        const int bytes = field_bytes(fields_[i]);
        if (bytes > widest_field) widest_field = bytes;
        const int group = frame::text_field_bytes("group", group_name(fields_[i].group));
        if (group > widest_group) widest_group = group;
    }
    frame::Budget budget(payload);
    return budget.take(frame::text_field_bytes("cmd", cmd_)) && budget.take(widest_group) &&
           budget.take(frame::int_field_bytes("part", count_)) &&
           budget.take(frame::kMoreFieldBytes) && budget.take(widest_field);
}

int DiagnosticsReport::next_frame(int payload, char* buf, int cap) {
    if (exhausted()) return 0;

    int room = payload + 1;
    if (room > cap) room = cap;

    const Group group = fields_[at_].group;
    frame::Budget budget(room - 1);
    budget.take(frame::text_field_bytes("cmd", cmd_));
    budget.take(frame::text_field_bytes("group", group_name(group)));
    budget.take(frame::int_field_bytes("part", part_));
    budget.take(frame::kMoreFieldBytes);
    int packed = 0;
    while (at_ + packed < count_ && fields_[at_ + packed].group == group &&
           budget.take(field_bytes(fields_[at_ + packed]))) {
        packed++;
    }
    if (packed == 0) return 0;

    json::Writer writer(buf, room);
    writer.kv_str("cmd", cmd_);
    writer.kv_str("group", group_name(group));
    writer.kv_int("part", part_);
    writer.kv_bool("more", at_ + packed < count_);
    for (int i = 0; i < packed; i++) {
        const Field& field = fields_[at_ + i];
        switch (field.kind) {
            case Kind::Int: writer.kv_int(field.key, field.value); break;
            case Kind::Bool: writer.kv_bool(field.key, field.value != 0); break;
            case Kind::Text: writer.kv_str(field.key, field.text); break;
        }
    }
    const int len = writer.finish();
    if (writer.overflowed()) return 0;

    at_ += packed;
    part_++;
    return len;
}

}  // namespace skyblip::comms
