#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_FLIGHT_LOG_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_FLIGHT_LOG_H

#include "core/comms/config.h"
#include "core/comms/log_link.h"
#include "core/flight/log_session.h"
#include "runtime/service.h"

namespace skyblip::go {

// A flight leaves a record. core/flight decides when a session runs and what a
// record holds; this puts the bytes on the external partition, finds the write
// frontier again after a power cut, and answers the tablet that comes to
// collect them.
class FlightLogService : public runtime::Service {
   public:
    // Enough entries for a season of gliding on one partition; a device with
    // more sessions than this on it offers the newest and says so.
    static constexpr int kMaxSessions = 16;

    using runtime::Service::Service;

    Status setup() override;
    void tick(uint32_t now_ms) override;

    // The prompt machine lives with the config service because the panel and
    // the button do, so a destructive erase is authorised the same way a
    // firmware upload is.
    void attach_config(comms::ConfigService& config) { config_ = &config; }

    bool available() const { return available_; }
    bool recording() const { return session_.open(); }
    uint32_t session_id() const { return session_.session_id(); }
    uint32_t records_written() const { return records_written_; }
    uint32_t records_dropped() const { return session_.dropped(); }
    uint32_t sessions_on_flash() const { return session_count_; }
    bool erasing() const { return erase_remaining_ > 0; }
    const flight::LogRing& ring() const { return ring_; }

    // How much of the partition the recovery scan had to read to find the write
    // frontier. Read by the test that says it does not scan the whole thing.
    uint32_t recovery_bytes_read() const { return recovery_bytes_read_; }

   private:
    struct SessionInfo {
        uint32_t session_id{0};
        uint32_t first_sector{0};
        uint32_t sectors{0};
        uint32_t records{0};
        bool closed{false};
    };

    void recover();
    void rebuild_index();
    void note_session(uint32_t session_id, uint32_t sector);
    // The first never-written slot in a sector, found by halving the sector
    // rather than reading it: records are appended in order, so "erased" is
    // monotone across the slots and eight reads answer what 170 would.
    uint32_t frontier_slot(uint32_t sector);
    bool sector_header(uint32_t sector, flight::LogSectorHeader& out);
    uint32_t sector_offset(uint32_t sector) const;

    bool claim_sector(uint32_t session_id);
    bool append(const flight::LogRecord& record);
    void drain();

    void serve_link();
    void handle(const comms::LogRequest& request);
    void answer_list(bool has_index, uint32_t index);
    void answer_read(uint32_t session_id, uint32_t from);
    void reply(const char* json, int len);
    void ack(bool ok, const char* reason);
    void begin_erase();
    void step_erase();
    bool on_ground() const;
    const SessionInfo* find(uint32_t session_id) const;

    comms::ConfigService* config_{nullptr};
    flight::LogSession session_{};
    flight::LogRing ring_{};

    SessionInfo index_[kMaxSessions]{};
    uint32_t session_count_{0};
    bool index_truncated_{false};

    uint32_t records_written_{0};
    uint32_t recovery_bytes_read_{0};
    uint32_t erase_remaining_{0};
    uint32_t erase_next_{0};
    // The session whose sector is currently claimed. Kept apart from the
    // policy's own view: a landing closes the session while its last record may
    // still be queued behind a transmit slot, and that record belongs in the
    // sector the session was writing.
    uint32_t claimed_session_{0};
    bool claimed_{false};
    bool available_{false};
    bool index_stale_{false};

    char reply_[comms::kLogReplyCap]{};
    uint8_t chunk_[comms::kLogChunkRawBytes]{};
    uint8_t scratch_[flight::kLogRecordBytes]{};
};

}  // namespace skyblip::go

#endif
