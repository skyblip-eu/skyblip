#include "products/skyblip_go/services/flight_log.h"

#include "core/util/span.h"

namespace skyblip::go {

Status FlightLogService::setup() {
    available_ = hal::has(context_.roles.capabilities, hal::Capability::Storage) &&
                 context_.roles.log_flash.ready();
    if (!available_) return Status::Ok;

    const uint32_t sector_bytes = context_.roles.log_flash.sector_bytes();
    const uint32_t slots = flight::log_slots_per_sector(sector_bytes);
    if (slots == 0) {
        available_ = false;
        return Status::Ok;
    }
    ring_.configure(context_.roles.log_flash.sector_count(), slots);
    recover();
    rebuild_index();
    return Status::Ok;
}

uint32_t FlightLogService::sector_offset(uint32_t sector) const {
    return sector * context_.roles.log_flash.sector_bytes();
}

bool FlightLogService::sector_header(uint32_t sector, flight::LogSectorHeader& out) {
    uint8_t raw[flight::kLogSectorHeaderBytes];
    if (!is_ok(context_.roles.log_flash.read(sector_offset(sector), raw, sizeof(raw))))
        return false;
    return is_ok(flight::decode_log_sector_header(raw, out));
}

// Only the labels. One 16-byte read per sector finds the write frontier in
// 5280 bytes of a 1.29 MB partition; reading the partition to find the end of it
// would be a quarter of a second of SPI on every boot.
void FlightLogService::recover() {
    const uint32_t count = ring_.sector_count();
    uint32_t best_sector = 0;
    uint32_t best_sequence = 0;
    bool found = false;
    for (uint32_t sector = 0; sector < count; sector++) {
        flight::LogSectorHeader header{};
        recovery_bytes_read_ += flight::kLogSectorHeaderBytes;
        if (!sector_header(sector, header)) continue;
        if (!found || header.sequence > best_sequence) {
            found = true;
            best_sequence = header.sequence;
            best_sector = sector;
        }
    }
    if (!found) {
        ring_.rewind();
        claimed_ = false;
        return;
    }
    // The frontier sector is left alone rather than appended to. A session owns
    // whole sectors, which is what lets an offload turn a record index into a
    // sector and a slot with two divisions instead of a table; the price is one
    // partly-used sector per power cut.
    ring_.restore(best_sector, ring_.slots_per_sector(), best_sequence);
    claimed_ = false;
}

uint32_t FlightLogService::frontier_slot(uint32_t sector) {
    const uint32_t base = sector_offset(sector);
    uint32_t low = 0;
    uint32_t high = ring_.slots_per_sector();
    while (low < high) {
        const uint32_t mid = low + (high - low) / 2;
        if (!is_ok(context_.roles.log_flash.read(base + flight::log_record_offset(mid), scratch_,
                                                 flight::kLogRecordBytes)))
            return low;
        if (flight::log_slot_erased(scratch_, flight::kLogRecordBytes))
            high = mid;
        else
            low = mid + 1;
    }
    return low;
}

void FlightLogService::note_session(uint32_t session_id, uint32_t sector) {
    const uint32_t count = ring_.sector_count();
    if (session_count_ > 0) {
        SessionInfo& last = index_[session_count_ - 1];
        const uint32_t after = (last.first_sector + last.sectors) % count;
        if (last.session_id == session_id && after == sector) {
            last.sectors++;
            return;
        }
    }
    if (session_count_ == kMaxSessions) {
        // Keep the newest: a pilot collecting logs wants last weekend, not the
        // first hop of the season, and the walk arrives in age order.
        for (int i = 1; i < kMaxSessions; i++) index_[i - 1] = index_[i];
        session_count_--;
        index_truncated_ = true;
    }
    SessionInfo& entry = index_[session_count_++];
    entry = SessionInfo{};
    entry.session_id = session_id;
    entry.first_sector = sector;
    entry.sectors = 1;
}

void FlightLogService::rebuild_index() {
    session_count_ = 0;
    index_truncated_ = false;
    if (!available_) return;

    const uint32_t count = ring_.sector_count();
    uint32_t oldest_sector = 0;
    uint32_t oldest_sequence = 0;
    bool found = false;
    for (uint32_t sector = 0; sector < count; sector++) {
        flight::LogSectorHeader header{};
        if (!sector_header(sector, header)) continue;
        if (!found || header.sequence < oldest_sequence) {
            found = true;
            oldest_sequence = header.sequence;
            oldest_sector = sector;
        }
    }
    if (!found) return;

    // Sectors are claimed in ring order, so walking the ring from the oldest
    // sequence walks the sessions in the order they were flown and every
    // session's sectors are consecutive.
    for (uint32_t step = 0; step < count; step++) {
        const uint32_t sector = (oldest_sector + step) % count;
        flight::LogSectorHeader header{};
        if (!sector_header(sector, header)) continue;
        note_session(header.session_id, sector);
    }

    const uint32_t slots = ring_.slots_per_sector();
    for (uint32_t i = 0; i < session_count_; i++) {
        SessionInfo& entry = index_[i];
        const uint32_t last_sector = (entry.first_sector + entry.sectors - 1) % count;
        uint32_t in_last = frontier_slot(last_sector);
        entry.closed = false;
        if (in_last > 0) {
            const uint32_t offset =
                sector_offset(last_sector) + flight::log_record_offset(in_last - 1);
            flight::LogRecord record{};
            if (is_ok(context_.roles.log_flash.read(offset, scratch_, flight::kLogRecordBytes))) {
                const Status decoded =
                    flight::decode_log_record(scratch_, entry.session_id, record);
                // Only the last write of a session can be torn, and a torn
                // record is not a record: it is not counted, so it is never
                // offered to a tablet as a position.
                if (decoded == Status::Crc)
                    in_last--;
                else if (is_ok(decoded))
                    entry.closed = record.session_end;
            }
        }
        entry.records = (entry.sectors - 1) * slots + in_last;
    }
}

bool FlightLogService::claim_sector(uint32_t session_id) {
    ring_.claim_next_sector();
    claimed_ = false;
    if (!is_ok(context_.roles.log_flash.erase_sector(ring_.sector()))) return false;

    flight::LogSectorHeader header{};
    header.sequence = ring_.sequence();
    header.session_id = session_id;
    uint8_t raw[flight::kLogSectorHeaderBytes];
    flight::encode_log_sector_header(header, raw);
    if (!is_ok(context_.roles.log_flash.write(sector_offset(ring_.sector()), raw, sizeof(raw))))
        return false;

    claimed_ = true;
    claimed_session_ = session_id;
    return true;
}

bool FlightLogService::append(const flight::LogRecord& record) {
    if (!claimed_ || ring_.sector_exhausted()) {
        if (!claim_sector(claimed_ ? claimed_session_ : session_.session_id())) return false;
    }
    flight::encode_log_record(record, claimed_session_, scratch_);
    const uint32_t offset = sector_offset(ring_.sector()) + flight::log_record_offset(ring_.slot());
    if (!is_ok(context_.roles.log_flash.write(offset, scratch_, flight::kLogRecordBytes)))
        return false;
    ring_.took_slot();
    records_written_++;
    return true;
}

void FlightLogService::drain() {
    // INFO: fl 03aug26 The log is on spi1 and the radio on spi3, and an external
    // NOR program does not halt the CPU the way an internal NVMC one does, so
    // these two cannot collide over a bus or a stalled core. The one window
    // still worth leaving alone is the direct slot, where own-ship may key the
    // PA: a record is four seconds of slack and a tick is ten milliseconds, so
    // waiting costs nothing and buys the rule its literal reading.
    if (context_.state.plan.tx_allowed) return;
    flight::LogRecord record{};
    while (session_.take(record)) {
        if (!append(record)) break;
    }
}

void FlightLogService::tick(uint32_t now_ms) {
    serve_link();

    if (config_ != nullptr && config_->log_erase_requested()) {
        config_->clear_log_erase_request();
        begin_erase();
    }
    if (erase_remaining_ > 0) {
        step_erase();
        return;
    }
    if (!available_) return;

    const flight::LogAction action = session_.update(context_.state.own, now_ms);
    if (action == flight::LogAction::OpenSession) claim_sector(session_.session_id());
    if (action == flight::LogAction::CloseSession) index_stale_ = true;
    drain();
    // Once the landing record is down, the device knows what it is carrying:
    // the scan costs a few milliseconds on the ground and it means the first
    // thing a tablet asks is answered from memory.
    if (index_stale_ && !session_.closing()) {
        rebuild_index();
        index_stale_ = false;
    }
}

void FlightLogService::begin_erase() {
    if (!available_) return;
    erase_remaining_ = ring_.sector_count();
    erase_next_ = 0;
}

void FlightLogService::step_erase() {
    // One sector per pass. A full partition is 330 erases of tens of
    // milliseconds each, and doing them in one tick would be a service that
    // stopped reporting progress for the better part of a minute.
    context_.roles.log_flash.erase_sector(erase_next_);
    erase_next_++;
    erase_remaining_--;
    if (erase_remaining_ > 0) return;

    ring_.rewind();
    session_.reset();
    claimed_ = false;
    session_count_ = 0;
    index_truncated_ = false;
    records_written_ = 0;
    ack(true, "erased");
}

bool FlightLogService::on_ground() const {
    if (config_ != nullptr) return config_->flight_state() == comms::FlightState::Ground;
    return comms::flight_state_from(context_.state.own.flight_state) == comms::FlightState::Ground;
}

const FlightLogService::SessionInfo* FlightLogService::find(uint32_t session_id) const {
    for (uint32_t i = 0; i < session_count_; i++)
        if (index_[i].session_id == session_id) return &index_[i];
    return nullptr;
}

int FlightLogService::payload() const {
    return static_cast<int>(context_.roles.link.payload_bytes());
}

int FlightLogService::reply_cap() const {
    const int room = payload() + 1;
    return room < comms::kLogReplyCap ? room : comms::kLogReplyCap;
}

void FlightLogService::reply(const char* json, int len) {
    if (len <= 0 || len > payload()) {
        link_drops_++;
        return;
    }
    if (!is_ok(context_.roles.link.send(
            messages::Endpoint::Log,
            ConstByteSpan(reinterpret_cast<const uint8_t*>(json), static_cast<size_t>(len)))))
        link_drops_++;
}

void FlightLogService::ack(bool ok, const char* reason) {
    reply(reply_, comms::format_log_ack(reply_, reply_cap(), ok, reason));
}

void FlightLogService::serve_link() {
    messages::RxFrame frame{};
    while (context_.bus.log_rx.pop(frame)) handle(comms::parse_log_request(frame));
}

// INFO: cf 03aug26 Every command is refused in the air, and not because the
// bytes would not fit: an offload is thousands of BLE round trips and a
// continuous read of the part, next to a receiver whose dwells are anchored to
// a PPS edge. The flight state that already gates a settings change and a
// firmware upload gates this too, so there is one answer to "what may a phone
// do to a flying device" rather than three.
void FlightLogService::handle(const comms::LogRequest& request) {
    if (!request.understood) {
        ack(false, "unknown_cmd");
        return;
    }
    if (!available_) {
        ack(false, "no_storage");
        return;
    }
    if (erase_remaining_ > 0) {
        ack(false, "busy");
        return;
    }
    if (!on_ground()) {
        ack(false, "in_flight");
        return;
    }

    switch (request.command) {
        case comms::LogCommand::List:
            if (!request.has_index) rebuild_index();
            answer_list(request.has_index, request.index);
            break;
        case comms::LogCommand::Read: answer_read(request.session, request.from); break;
        case comms::LogCommand::Erase:
            if (config_ != nullptr)
                config_->request_log_erase();
            else
                ack(false, "no_prompt");
            break;
        case comms::LogCommand::None: ack(false, "unknown_cmd"); break;
    }
}

// One command, one frame. Sixteen notifications in a single pass would outrun
// the controller's buffers and the lines that did not fit would be lost without
// anyone noticing, so the tablet asks for the count and then for each line.
void FlightLogService::answer_list(bool has_index, uint32_t index) {
    if (!has_index) {
        reply(reply_,
              comms::format_log_count(reply_, reply_cap(), session_count_, index_truncated_));
        return;
    }
    if (index >= session_count_) {
        ack(false, "no_session");
        return;
    }
    const SessionInfo& entry = index_[index];
    reply(reply_, comms::format_log_session(reply_, reply_cap(), index, session_count_,
                                            entry.session_id, entry.records, entry.closed));
}

void FlightLogService::answer_read(uint32_t session_id, uint32_t from) {
    const SessionInfo* entry = find(session_id);
    if (entry == nullptr) {
        rebuild_index();
        entry = find(session_id);
    }
    if (entry == nullptr) {
        ack(false, "no_session");
        return;
    }
    if (from >= entry->records) {
        reply(reply_,
              comms::format_log_chunk(reply_, reply_cap(), session_id, from, chunk_, 0, true));
        return;
    }

    // INFO: fc 04aug26 The chunk is as many records as this link's payload holds,
    // which is a throughput win at the top of the range and the difference
    // between working and not at the bottom of it: the fixed five were sized
    // against a 236-byte guess, and an iPhone carries 182.
    const int per_chunk = comms::log_records_per_chunk(payload());
    if (per_chunk == 0) {
        ack(false, "payload");
        return;
    }

    const uint32_t slots = ring_.slots_per_sector();
    const uint32_t count = ring_.sector_count();
    const uint32_t available = entry->records - from;
    const uint32_t wanted =
        available < static_cast<uint32_t>(per_chunk) ? available : static_cast<uint32_t>(per_chunk);
    for (uint32_t i = 0; i < wanted; i++) {
        const uint32_t index = from + i;
        const uint32_t sector = (entry->first_sector + index / slots) % count;
        const uint32_t offset = sector_offset(sector) + flight::log_record_offset(index % slots);
        if (!is_ok(context_.roles.log_flash.read(offset, chunk_ + i * flight::kLogRecordBytes,
                                                 flight::kLogRecordBytes))) {
            ack(false, "read_failed");
            return;
        }
    }
    reply(reply_,
          comms::format_log_chunk(reply_, reply_cap(), session_id, from, chunk_,
                                  static_cast<int>(wanted), from + wanted >= entry->records));
}

}  // namespace skyblip::go
