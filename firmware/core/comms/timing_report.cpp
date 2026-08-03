#include "core/comms/timing_report.h"

#include "core/util/format.h"
#include "core/util/json_min.h"

namespace skyblip::comms {

namespace {

int text_bytes(const char* s) {
    int n = 0;
    while (s[n] != 0) n++;
    return n;
}

int number_bytes(long v) {
    int n = v < 0 ? 1 : 0;
    unsigned long magnitude =
        v < 0 ? static_cast<unsigned long>(-v) : static_cast<unsigned long>(v);
    do {
        n++;
        magnitude /= 10;
    } while (magnitude != 0);
    return n;
}

// INFO: fc 04aug26 What is left of a frame, counted the way json::Writer spends
// it: the two braces up front, then each field and the comma that joins it to
// the one before. This is why a frame follows the MTU the central negotiated
// instead of the size of a local buffer.
class Budget {
   public:
    explicit Budget(int payload) : left_(payload - 2) {}

    bool take(int bytes) {
        const int need = bytes + (first_ ? 0 : 1);
        if (need > left_) return false;
        left_ -= need;
        first_ = false;
        return true;
    }

   private:
    int left_;
    bool first_{true};
};

// "more":false, the longer of the two, so a frame that ends up carrying
// "more":true has a byte to spare rather than a byte too few.
constexpr int kMoreFieldBytes = 6 + 1 + 5;

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
    const int value = f.text != nullptr ? text_bytes(f.text) + 2 : number_bytes(f.value);
    return text_bytes(f.key) + 2 + 1 + value;
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

    add("pps_us", pps_, 0);
    add("pps_worst_us", nullptr, stats.pps_worst_us());
    add("pps_samples", nullptr, static_cast<long>(stats.pps_samples()));
    add("dwell_us", dwell_, 0);
    add("dwell_worst_us", nullptr, stats.dwell_worst_us());
    add("dwell_samples", nullptr, static_cast<long>(stats.dwell_samples()));
    add("holdover", nullptr, static_cast<long>(stats.holdover_events()));
    add("missed", nullptr, static_cast<long>(stats.missed()));
    add("refused", nullptr, static_cast<long>(stats.refused()));
    add("carrier_sense_dbm", nullptr, carrier_sense_dbm);
    add("carrier_sense_ceiling_dbm", nullptr, timing::NoiseFloor::kThresholdCeilingDbm);
    add("carrier_sense_us", nullptr, static_cast<long>(timing::CarrierSense::kAssessmentUs));
}

bool TimingReport::fits(int payload) const {
    int widest = 0;
    for (int i = 0; i < count_; i++) {
        const int bytes = field_bytes(fields_[i]);
        if (bytes > widest) widest = bytes;
    }
    Budget budget(payload);
    return budget.take(field_bytes(Field{"cmd", "timing", 0})) &&
           budget.take(field_bytes(Field{"part", nullptr, count_})) &&
           budget.take(kMoreFieldBytes) && budget.take(widest);
}

int TimingReport::next_frame(int payload, char* buf, int cap) {
    if (exhausted()) return 0;

    int room = payload + 1;
    if (room > cap) room = cap;

    Budget budget(room - 1);
    budget.take(field_bytes(Field{"cmd", "timing", 0}));
    budget.take(field_bytes(Field{"part", nullptr, part_}));
    budget.take(kMoreFieldBytes);
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
