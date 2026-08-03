#include "core/comms/log_link.h"

#include <cstring>

#include "core/util/json_min.h"

namespace skyblip::comms {

namespace {

const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

uint32_t non_negative(long v) { return v < 0 ? 0u : static_cast<uint32_t>(v); }

}  // namespace

int base64_encode(const uint8_t* in, int len, char* out, int cap) {
    const int needed = ((len + 2) / 3) * 4;
    if (needed + 1 > cap) return -1;
    int n = 0;
    for (int i = 0; i < len; i += 3) {
        const uint32_t b0 = in[i];
        const uint32_t b1 = i + 1 < len ? in[i + 1] : 0;
        const uint32_t b2 = i + 2 < len ? in[i + 2] : 0;
        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out[n++] = kAlphabet[(triple >> 18) & 0x3F];
        out[n++] = kAlphabet[(triple >> 12) & 0x3F];
        out[n++] = i + 1 < len ? kAlphabet[(triple >> 6) & 0x3F] : '=';
        out[n++] = i + 2 < len ? kAlphabet[triple & 0x3F] : '=';
    }
    out[n] = 0;
    return n;
}

LogRequest parse_log_request(const messages::RxFrame& frame) {
    LogRequest request{};
    if (frame.endpoint != messages::Endpoint::Log) return request;

    json::Reader reader(reinterpret_cast<const char*>(frame.data.data()), frame.len);
    char command[16] = {0};
    if (!reader.get_str("cmd", command, sizeof(command))) return request;

    if (std::strcmp(command, "list") == 0) {
        request.command = LogCommand::List;
        long value = 0;
        request.has_index = reader.get_int("index", value);
        request.index = request.has_index ? non_negative(value) : 0;
    } else if (std::strcmp(command, "erase") == 0) {
        request.command = LogCommand::Erase;
    } else if (std::strcmp(command, "read") == 0) {
        request.command = LogCommand::Read;
        long value = 0;
        if (!reader.get_int("session", value)) return request;
        request.session = non_negative(value);
        request.from = reader.get_int("from", value) ? non_negative(value) : 0;
    } else {
        return request;
    }
    request.understood = true;
    return request;
}

int format_log_ack(char* buf, int cap, bool ok, const char* reason) {
    json::Writer writer(buf, cap);
    writer.kv_str("cmd", "log");
    writer.kv_bool("ack", ok);
    if (reason != nullptr) writer.kv_str("reason", reason);
    return writer.finish();
}

int format_log_count(char* buf, int cap, uint32_t sessions, bool truncated) {
    json::Writer writer(buf, cap);
    writer.kv_str("cmd", "log");
    writer.kv_bool("ack", true);
    writer.kv_int("sessions", static_cast<long>(sessions));
    // The partition holds more flights than the index offers. Said out loud,
    // because a tablet that shows sixteen when there are twenty has lied.
    writer.kv_bool("truncated", truncated);
    return writer.finish();
}

int format_log_session(char* buf, int cap, uint32_t index, uint32_t count, uint32_t session_id,
                       uint32_t records, bool closed) {
    json::Writer writer(buf, cap);
    writer.kv_str("cmd", "session");
    writer.kv_int("index", static_cast<long>(index));
    writer.kv_int("of", static_cast<long>(count));
    // The session's opening UTC second: its name, and the base every one of its
    // records is timed against.
    writer.kv_int("session", static_cast<long>(session_id));
    writer.kv_int("records", static_cast<long>(records));
    // False means the log stops where the power did. The tablet says so instead
    // of presenting a truncated flight as a complete one.
    writer.kv_bool("closed", closed);
    return writer.finish();
}

int format_log_chunk(char* buf, int cap, uint32_t session_id, uint32_t from, const uint8_t* raw,
                     int record_count, bool eof) {
    char data[kLogChunkRawBytes * 4 / 3 + 8];
    const int encoded = base64_encode(raw, record_count * static_cast<int>(flight::kLogRecordBytes),
                                      data, static_cast<int>(sizeof(data)));
    if (encoded < 0) return 0;

    json::Writer writer(buf, cap);
    writer.kv_str("cmd", "chunk");
    writer.kv_int("session", static_cast<long>(session_id));
    writer.kv_int("from", static_cast<long>(from));
    writer.kv_int("n", record_count);
    writer.kv_bool("eof", eof);
    writer.kv_str("data", data);
    return writer.finish();
}

}  // namespace skyblip::comms
