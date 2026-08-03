// core/flight/log_session.h: when a flight is worth recording, and where the
// next record goes. Both are policy, both are pure, and neither knows what
// flash is - the service does the writing and this decides what to write.
//
// A device parked on a trailer for a week must not fill the partition with its
// own stillness, so a session is bracketed by core/flight's flight state and by
// nothing else. That state is already the one the transmitter and the update
// lockout obey, so "the log started" and "the aircraft took off" cannot drift
// apart.
#ifndef SKYBLIP_CORE_FLIGHT_LOG_SESSION_H
#define SKYBLIP_CORE_FLIGHT_LOG_SESSION_H

#include "core/flight/log_record.h"
#include "core/flight/state.h"

namespace skyblip::flight {

// INFO: fl 03aug26 Four seconds. It is the interval the moshe-braner SoftRF
// fork ships as its default (oss/SoftRF-moshe-braner .../src/driver/
// Settings.cpp:838 loginterval = 4) and it is inside every fix-interval a
// badge, an OLC claim or a competition file is scored on. At 100 kt it puts a
// fix every 200 m, which rounds a turn point to a tenth of the observation
// zone; halving it would double the bytes to buy resolution nothing scores.
constexpr uint32_t kLogRecordPeriodMs = 4000;

// INFO: fl 03aug26 A takeoff is only declared five seconds after the motion
// criterion is met (kTakeoffHoldMs), so a log that started at the declaration
// would open in the climb-out with the ground roll missing. Eight samples of
// slack is 32 seconds held in RAM and handed to the file the moment it opens -
// the same trick the moshe-braner fork plays with its pre-position ring
// (oss/SoftRF-moshe-braner .../src/protocol/data/IGC.cpp:1105-1125).
constexpr int kLogPreTakeoffRecords = 8;

enum class LogAction : uint8_t { Idle, OpenSession, AppendRecord, CloseSession };

// Fed own-ship every service pass. Holds the records it wants written in a
// bounded ring; the writer drains them when the radio is not keying.
class LogSession {
   public:
    LogAction update(const messages::OwnState& own, uint32_t now_ms);

    bool open() const { return open_; }
    // A closed session whose last record has not reached flash yet.
    bool closing() const { return closing_; }
    // The UTC second the session opened, which is also the time base every one
    // of its records is written against and the name the offload protocol uses.
    uint32_t session_id() const { return session_id_; }

    int queued() const { return count_; }
    // Yields records only while a session is running, plus the one last record a
    // landing leaves behind. On the ground the same ring is a holding pen, not a
    // queue: what it holds is overwritten, never written out.
    bool take(LogRecord& out);

    // Records the ring had to overwrite because nothing drained it. Not silent:
    // the same accounting the bus queues get.
    uint32_t dropped() const { return dropped_; }

    // After an erase there is no history to be the tail of.
    void reset();

   private:
    void push(const LogRecord& record);

    LogRecord ring_[kLogPreTakeoffRecords]{};
    int head_{0};
    int count_{0};
    uint32_t dropped_{0};
    uint32_t session_id_{0};
    uint32_t last_sample_ms_{0};
    bool sampled_{false};
    bool open_{false};
    bool closing_{false};
};

// The write frontier. The partition is a ring of sectors: when the last one
// fills, the oldest is erased and reused, so a device nobody ever offloads keeps
// the most recent hours instead of quietly stopping at the first landing that
// filled it. The sequence number never repeats, which is what lets a boot find
// the frontier from the sector labels alone.
class LogRing {
   public:
    void configure(uint32_t sector_count, uint32_t slots_per_sector);

    // What recovery found: the sector holding the highest sequence, the first
    // free slot in it, and that sequence.
    void restore(uint32_t sector, uint32_t slot, uint32_t sequence);
    void rewind();

    uint32_t sector() const { return sector_; }
    uint32_t slot() const { return slot_; }
    uint32_t sequence() const { return sequence_; }
    uint32_t sector_count() const { return sector_count_; }
    uint32_t slots_per_sector() const { return slots_per_sector_; }

    bool configured() const { return sector_count_ > 0 && slots_per_sector_ > 0; }
    // Whether any sector has been claimed at all. A virgin partition has not,
    // which is why the first claim takes sector zero rather than sector one.
    bool claimed() const { return claimed_; }
    bool sector_exhausted() const { return slot_ >= slots_per_sector_; }

    // Move to the next sector, wrapping, and take the next sequence number. The
    // caller erases it and writes its header before any record lands in it.
    void claim_next_sector();
    void took_slot() { slot_++; }

   private:
    uint32_t sector_count_{0};
    uint32_t slots_per_sector_{0};
    uint32_t sector_{0};
    uint32_t slot_{0};
    uint32_t sequence_{0};
    bool claimed_{false};
};

// How long the reserved partition holds, for the arithmetic nobody should have
// to redo in their head: 170 records of 24 bytes every four seconds is
// 21.6 KB/h, so a mebibyte is a little over 48 flight hours.
constexpr uint32_t log_seconds_per_sector(uint32_t slots_per_sector) {
    return slots_per_sector * (kLogRecordPeriodMs / 1000);
}
constexpr uint32_t log_seconds_for(uint32_t sector_count, uint32_t slots_per_sector) {
    return sector_count * log_seconds_per_sector(slots_per_sector);
}

}  // namespace skyblip::flight

#endif
