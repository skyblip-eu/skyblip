// core/comms/log_link.h: the flight log's half of the companion link. Same JSON
// dialect core/comms/config.h speaks, on its own endpoint, with three commands:
// list, read, erase.
//
// Every reply fits in one frame and every read is asked for by the tablet, one
// chunk at a time, carrying the index it wants next. That makes the transfer
// acknowledged by construction - the next command IS the acknowledgement - and
// resumable at no cost: a dropped connection is a tablet that asks again from
// the last index it kept. It is also what makes a failed send harmless here: a
// chunk that never left consumed nothing on the device, so the tablet asking for
// the same index again is the whole recovery.
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

// INFO: fc 04aug26 The widest frame this dialect can be asked to build, which is
// a buffer bound and not a promise: what actually goes out is cut to the payload
// hal::Link::payload_bytes() reports. Room for the largest chunk a link that
// negotiated ATT_MTU 498 can carry.
constexpr int kLogReplyCap = 512;

// INFO: cf 03aug26 Raw bytes are what travels, CRC and all, so the host verifies
// each record against the same checksum the flash holds - the transfer is checked
// end to end rather than hop by hop.
//
// INFO: fc 04aug26 How many of them ride in one chunk is derived from the
// negotiated payload, not fixed at five: five was chosen against a 256-byte
// buffer, which an iPhone's 182 bytes cannot carry and a large-MTU phone would be
// short-changed by. Twelve is the ceiling because that is what a 498-byte ATT_MTU
// reaches; the base64 of 24 raw bytes is exactly 32 characters, so the arithmetic
// below is exact rather than an estimate.
constexpr int kLogRecordsPerChunkMax = 12;
constexpr int kLogChunkRawBytes =
    kLogRecordsPerChunkMax * static_cast<int>(flight::kLogRecordBytes);
constexpr int kLogChunkBase64PerRecord = static_cast<int>(flight::kLogRecordBytes) / 3 * 4;

// The chunk envelope at its widest: the longest session id and record index the
// partition can produce, the two-digit count, and an empty data string.
constexpr int kLogChunkEnvelopeBytes = 83;

// How many records a chunk may carry over a link that negotiated this payload.
// Zero means not even one fits, which is a refusal for the caller to count.
int log_records_per_chunk(int payload_bytes);

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
