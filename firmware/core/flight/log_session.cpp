#include "core/flight/log_session.h"

namespace skyblip::flight {

void LogSession::push(const LogRecord& record) {
    if (count_ == kLogPreTakeoffRecords) {
        head_ = (head_ + 1) % kLogPreTakeoffRecords;
        count_--;
        // On the ground the ring is meant to turn over - that is what makes it a
        // pre-takeoff window. Only a record the writer owed and did not take is
        // a loss worth counting.
        if (open_ || closing_) dropped_++;
    }
    ring_[(head_ + count_) % kLogPreTakeoffRecords] = record;
    count_++;
}

bool LogSession::take(LogRecord& out) {
    if (!open_ && !closing_) return false;
    if (count_ == 0) {
        closing_ = false;
        return false;
    }
    out = ring_[head_];
    head_ = (head_ + 1) % kLogPreTakeoffRecords;
    count_--;
    if (!open_ && count_ == 0) closing_ = false;
    return true;
}

void LogSession::reset() {
    head_ = 0;
    count_ = 0;
    session_id_ = 0;
    sampled_ = false;
    open_ = false;
    closing_ = false;
}

LogAction LogSession::update(const messages::OwnState& own, uint32_t now_ms) {
    const bool airborne = own.flight_state == static_cast<uint8_t>(FlightState::Airborne);
    // A record with no position or no UTC is a row of zeroes in a flight log.
    // The session survives a fix outage - the aircraft is still where it was -
    // but nothing is written across it.
    const bool usable = own.fix_valid && own.utc_valid;

    if (open_ && !airborne) {
        if (usable) {
            LogRecord last = log_record_from(own);
            last.session_end = true;
            push(last);
        }
        open_ = false;
        closing_ = true;
        return LogAction::CloseSession;
    }

    if (!usable) return LogAction::Idle;
    if (sampled_ && now_ms - last_sample_ms_ < kLogRecordPeriodMs) return LogAction::Idle;
    last_sample_ms_ = now_ms;
    sampled_ = true;

    const LogRecord record = log_record_from(own);
    if (!airborne && !open_) {
        // On the ground and staying there: kept in RAM, never written, and
        // overwritten by the next one. This is the whole answer to "the device
        // does not log while it is parked".
        push(record);
        return LogAction::Idle;
    }

    if (!open_) {
        // The oldest sample the ring still holds names the session, so the file
        // begins where the aircraft began moving rather than where the criterion
        // finally agreed. Named after the push, not before it: the push is what
        // decides which sample is the oldest one that survived.
        push(record);
        session_id_ = ring_[head_].utc;
        open_ = true;
        closing_ = false;
        return LogAction::OpenSession;
    }

    push(record);
    return LogAction::AppendRecord;
}

void LogRing::configure(uint32_t sector_count, uint32_t slots_per_sector) {
    sector_count_ = sector_count;
    slots_per_sector_ = slots_per_sector;
    rewind();
}

void LogRing::rewind() {
    sector_ = 0;
    slot_ = 0;
    sequence_ = 0;
    claimed_ = false;
}

void LogRing::restore(uint32_t sector, uint32_t slot, uint32_t sequence) {
    sector_ = sector;
    slot_ = slot;
    sequence_ = sequence;
    claimed_ = true;
}

void LogRing::claim_next_sector() {
    if (!configured()) return;
    // The very first sector of a virgin partition is sector zero, not sector
    // one: nothing has been claimed yet, so there is nothing to step past.
    if (claimed_) sector_ = sector_ + 1 >= sector_count_ ? 0 : sector_ + 1;
    claimed_ = true;
    sequence_++;
    slot_ = 0;
}

}  // namespace skyblip::flight
