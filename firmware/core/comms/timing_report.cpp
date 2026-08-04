#include "core/comms/timing_report.h"

#include "core/comms/frame_budget.h"
#include "core/util/format.h"
#include "core/util/json_min.h"

namespace skyblip::comms {

namespace {

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

int TimingReport::field_bytes(const Field& f) {
    return f.text != nullptr ? frame::text_field_bytes(f.key, f.text)
                             : frame::int_field_bytes(f.key, f.value);
}

void TimingReport::add(const char* key, const char* text, long value) {
    if (count_ >= kMaxFields) return;
    fields_[count_++] = Field{key, text, value};
}

// pps_us and dwell_us are the two histograms in kBuckets order: the SX1262's own
// retune time, core/timing::kHopGuardMs and core/timing::kJitterGuardMs on each
// side of the centre bucket (core/timing/timing_stats.h). holdover, missed and
// refused are counted apart from both, on purpose: a fault a histogram cannot
// bound must not be folded into one that can.
TimingReport::TimingReport(const timing::SlotTimingStats& stats, int8_t carrier_sense_dbm) {
    format_buckets(stats, true, pps_, kBucketsTextCap);
    format_buckets(stats, false, dwell_, kBucketsTextCap);

    // Every count goes through frame::counter and not through a cast: the worst_us
    // pair is signed on both platforms and stays as it is, but a uint32_t sample
    // count cast straight to long is a negative number on the nRF52 and a correct
    // one in this suite (core/comms/frame_budget.h).
    add("pps_us", pps_, 0);
    add("pps_worst_us", nullptr, stats.pps_worst_us());
    add("pps_samples", nullptr, frame::counter(stats.pps_samples()));
    add("dwell_us", dwell_, 0);
    add("dwell_worst_us", nullptr, stats.dwell_worst_us());
    add("dwell_samples", nullptr, frame::counter(stats.dwell_samples()));
    add("holdover", nullptr, frame::counter(stats.holdover_events()));
    add("missed", nullptr, frame::counter(stats.missed()));
    add("refused", nullptr, frame::counter(stats.refused()));
    add("carrier_sense_dbm", nullptr, carrier_sense_dbm);
    add("carrier_sense_ceiling_dbm", nullptr, timing::NoiseFloor::kThresholdCeilingDbm);
    add("carrier_sense_us", nullptr, frame::counter(timing::CarrierSense::kAssessmentUs));
}

bool TimingReport::fits(int payload) const {
    int widest = 0;
    for (int i = 0; i < count_; i++) {
        const int bytes = field_bytes(fields_[i]);
        if (bytes > widest) widest = bytes;
    }
    frame::Budget budget(payload);
    return budget.take(field_bytes(Field{"cmd", "timing", 0})) &&
           budget.take(field_bytes(Field{"part", nullptr, count_})) &&
           budget.take(frame::kMoreFieldBytes) && budget.take(widest);
}

int TimingReport::next_frame(int payload, char* buf, int cap) {
    if (exhausted()) return 0;

    int room = payload + 1;
    if (room > cap) room = cap;

    frame::Budget budget(room - 1);
    budget.take(field_bytes(Field{"cmd", "timing", 0}));
    budget.take(field_bytes(Field{"part", nullptr, part_}));
    budget.take(frame::kMoreFieldBytes);
    int packed = 0;
    while (at_ + packed < count_ && budget.take(field_bytes(fields_[at_ + packed]))) packed++;
    if (packed == 0) return 0;

    json::Writer writer(buf, room);
    writer.kv_str("cmd", "timing");
    writer.kv_int("part", part_);
    writer.kv_bool("more", at_ + packed < count_);
    for (int i = 0; i < packed; i++) {
        const Field& field = fields_[at_ + i];
        if (field.text != nullptr)
            writer.kv_str(field.key, field.text);
        else
            writer.kv_int(field.key, field.value);
    }
    const int len = writer.finish();
    if (writer.overflowed()) return 0;

    at_ += packed;
    part_++;
    return len;
}

}  // namespace skyblip::comms
