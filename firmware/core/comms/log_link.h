// core/comms/log_link.h: the flight log's half of the companion link. Same JSON
// dialect core/comms/config.h speaks, on its own endpoint, with three commands:
// list, read, erase.
//
// Every reply fits in one frame and every read is asked for by the tablet, one
// chunk at a time, carrying the index it wants next. That makes the transfer
// acknowledged by construction - the next command IS the acknowledgement - and
// resumable at no cost: a dropped connection is a tablet that asks again from
// the last index it kept.
#ifndef SKYBLIP_CORE_COMMS_LOG_LINK_H
#define SKYBLIP_CORE_COMMS_LOG_LINK_H

#include "core/flight/log_record.h"
#include "core/messages/messages.h"

namespace skyblip::comms {

enum class LogCommand : uint8_t { None, List, Read, Erase };

struct LogRequest {
    LogCommand command{LogCommand::None};
    uint32_t session{0};
    uint32_t from{0};
    // A bare list asks how many flights there are; a list with an index asks
    // about one of them. One command, one frame, either way.
    uint32_t index{0};
    bool has_index{false};
    bool understood{false};
};

// One reply, one frame. 256 bytes is what messages::RxFrame carries and what a
// BLE notify reaches with a negotiated MTU; nothing here is allowed to need two.
constexpr int kLogReplyCap = 256;

// INFO: cf 03aug26 Five records: 120 raw bytes become 160 base64 characters,
// which with the envelope leaves ~20 bytes of slack in a 256-byte frame. Raw
// bytes are what travels, CRC and all, so the host verifies each record against
// the same checksum the flash holds - the transfer is checked end to end rather
// than hop by hop.
constexpr int kLogRecordsPerChunk = 5;
constexpr int kLogChunkRawBytes = kLogRecordsPerChunk * static_cast<int>(flight::kLogRecordBytes);

LogRequest parse_log_request(const messages::RxFrame& frame);

// Returns the number of characters written, excluding the terminator.
int format_log_ack(char* buf, int cap, bool ok, const char* reason);
int format_log_count(char* buf, int cap, uint32_t sessions, bool truncated);
int format_log_session(char* buf, int cap, uint32_t index, uint32_t count, uint32_t session_id,
                       uint32_t records, bool closed);
int format_log_chunk(char* buf, int cap, uint32_t session_id, uint32_t from, const uint8_t* raw,
                     int record_count, bool eof);

// Standard base64, no padding omitted, no line breaks. Returns characters
// written, or -1 if they would not fit.
int base64_encode(const uint8_t* in, int len, char* out, int cap);

}  // namespace skyblip::comms

#endif
