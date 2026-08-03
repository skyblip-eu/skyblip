// The flight log's pure half: what a record is on flash, when a session runs,
// where the next one goes, and what the tablet is told. No device, no flash, no
// service - if any of this needs a board to be checked, it is in the wrong layer.
#include <cstring>

#include "core/comms/config.h"
#include "core/comms/log_link.h"
#include "core/flight/log_record.h"
#include "core/flight/log_session.h"
#include "doctest/doctest.h"
#include "hal/link.h"

using namespace skyblip;

namespace {

constexpr uint32_t kBaseUtc = 1785628800;  // 2026-08-02T00:00:00Z

flight::LogRecord sample_record() {
    flight::LogRecord r{};
    r.utc = kBaseUtc + 1234;
    r.lat_1e7 = 485212345;
    r.lon_1e7 = -21234567;
    r.alt_msl_m = 1487;
    r.alt_hae_m = 1487 + 46;
    r.speed_q = 253;
    r.track_c9 = 401;
    r.climb_e8 = -37;
    r.hdop_e2 = 120;
    r.sats = 11;
    r.flight_state = 2;
    r.fix_valid = true;
    r.utc_valid = true;
    r.pps_locked = true;
    r.climb_valid = true;
    r.geoid_separation_measured = true;
    return r;
}

messages::OwnState flying(uint32_t utc, uint16_t speed_q) {
    messages::OwnState own{};
    own.fix_valid = true;
    own.utc_valid = true;
    own.utc = utc;
    own.lat_1e7 = 485000000;
    own.lon_1e7 = 85000000;
    own.alt_m = 1000;
    own.alt_msl_m = 954;
    own.speed_q = speed_q;
    own.track_c9 = 128;
    own.sats = 10;
    own.hdop_e2 = 100;
    own.flight_state = static_cast<uint8_t>(flight::FlightState::Airborne);
    return own;
}

messages::OwnState parked(uint32_t utc) {
    messages::OwnState own = flying(utc, 0);
    own.flight_state = static_cast<uint8_t>(flight::FlightState::OnGround);
    return own;
}

}  // namespace

TEST_CASE("log record: twenty-four bytes carry the instant own-ship publishes") {
    const flight::LogRecord in = sample_record();
    uint8_t raw[flight::kLogRecordBytes];
    flight::encode_log_record(in, kBaseUtc, raw);

    flight::LogRecord out{};
    REQUIRE(flight::decode_log_record(raw, kBaseUtc, out) == Status::Ok);
    CHECK(out.utc == in.utc);
    CHECK(out.lat_1e7 == in.lat_1e7);
    CHECK(out.lon_1e7 == in.lon_1e7);
    CHECK(out.alt_msl_m == in.alt_msl_m);
    // Both datums survive: the ellipsoidal height is stored as its distance from
    // mean sea level, so a reader gets back the pair own-ship published.
    CHECK(out.alt_hae_m == in.alt_hae_m);
    CHECK(out.speed_q == in.speed_q);
    CHECK(out.track_c9 == in.track_c9);
    CHECK(out.climb_e8 == in.climb_e8);
    CHECK(out.sats == in.sats);
    CHECK(out.hdop_e2 == in.hdop_e2);
    CHECK(out.flight_state == in.flight_state);
    CHECK(out.pps_locked);
    CHECK(out.geoid_separation_measured);
    CHECK_FALSE(out.session_end);
}

TEST_CASE("log record: the budget the partition was sized on") {
    // 170 records and a 16-byte label in a 4 KB sector, to the byte.
    CHECK(flight::kLogSlotsPerSector == 170);
    CHECK(flight::kLogSectorHeaderBytes + flight::kLogSlotsPerSector * flight::kLogRecordBytes ==
          flight::kLogSectorBytes);
    // A four-second record period puts 11 minutes 20 seconds in a sector, so the
    // 330 sectors of log_partition hold 62 hours and a mebibyte holds 48.
    CHECK(flight::log_seconds_per_sector(flight::kLogSlotsPerSector) == 680);
    CHECK(flight::log_seconds_for(330, flight::kLogSlotsPerSector) == 224400);
    CHECK(
        flight::log_seconds_for(1024 * 1024 / flight::kLogSectorBytes, flight::kLogSlotsPerSector) /
            3600 ==
        48);
}

TEST_CASE("log record: an erased slot is empty and a torn one is a checksum failure") {
    uint8_t erased[flight::kLogRecordBytes];
    std::memset(erased, 0xFF, sizeof(erased));
    flight::LogRecord out{};
    // Never mistaken for a position, and never even asked the CRC: on NOR this
    // is what "nothing has been written here" looks like.
    CHECK(flight::decode_log_record(erased, kBaseUtc, out) == Status::Empty);

    uint8_t raw[flight::kLogRecordBytes];
    flight::encode_log_record(sample_record(), kBaseUtc, raw);
    // The cell died halfway through the program: the bytes that made it are
    // real, the rest is still erased.
    for (size_t i = 9; i < sizeof(raw); i++) raw[i] = 0xFF;
    CHECK(flight::decode_log_record(raw, kBaseUtc, out) == Status::Crc);

    // And a single flipped bit anywhere in the payload is caught too.
    flight::encode_log_record(sample_record(), kBaseUtc, raw);
    raw[5] ^= 0x01;
    CHECK(flight::decode_log_record(raw, kBaseUtc, out) == Status::Crc);
}

TEST_CASE("log record: a session longer than the offset can count saturates rather than wraps") {
    flight::LogRecord in = sample_record();
    in.utc = kBaseUtc + 100000;
    uint8_t raw[flight::kLogRecordBytes];
    flight::encode_log_record(in, kBaseUtc, raw);
    flight::LogRecord out{};
    REQUIRE(flight::decode_log_record(raw, kBaseUtc, out) == Status::Ok);
    // Eighteen hours in, the record reads as eighteen hours in and not as the
    // start of the flight.
    CHECK(out.utc == kBaseUtc + 0xFFFF);
}

TEST_CASE("log record: a sector label survives, and a half-written one is refused") {
    flight::LogSectorHeader in{};
    in.sequence = 4242;
    in.session_id = kBaseUtc;
    uint8_t raw[flight::kLogSectorHeaderBytes];
    flight::encode_log_sector_header(in, raw);

    flight::LogSectorHeader out{};
    REQUIRE(flight::decode_log_sector_header(raw, out) == Status::Ok);
    CHECK(out.sequence == 4242);
    CHECK(out.session_id == kBaseUtc);
    CHECK(out.record_bytes == flight::kLogRecordBytes);

    uint8_t erased[flight::kLogSectorHeaderBytes];
    std::memset(erased, 0xFF, sizeof(erased));
    CHECK(flight::decode_log_sector_header(erased, out) == Status::Empty);

    raw[6] ^= 0x40;
    CHECK(flight::decode_log_sector_header(raw, out) == Status::Crc);
}

TEST_CASE("log session: a device parked on a trailer writes nothing") {
    flight::LogSession session;
    for (uint32_t i = 0; i < 100; i++) {
        CHECK(session.update(parked(kBaseUtc + i), i * 1000) == flight::LogAction::Idle);
    }
    CHECK_FALSE(session.open());
    // What it did keep is the last few seconds, in RAM, against a takeoff.
    CHECK(session.queued() == flight::kLogPreTakeoffRecords);
}

TEST_CASE("log session: on the ground the ring is a holding pen and not a queue") {
    flight::LogSession session;
    for (uint32_t i = 0; i < 4; i++) session.update(parked(kBaseUtc + i * 4), i * 4000);
    CHECK(session.queued() == 4);
    flight::LogRecord out{};
    // Nothing may be written out: this is the whole difference between a device
    // that keeps the last thirty seconds against a takeoff and a device that
    // fills its partition sitting in a trailer.
    CHECK_FALSE(session.take(out));
}

TEST_CASE("log session: a record every four seconds and no more") {
    flight::LogSession session;
    session.update(flying(kBaseUtc, 200), 0);
    flight::LogRecord drained{};
    while (session.take(drained)) {
    }

    CHECK(session.update(flying(kBaseUtc + 1, 200), 1000) == flight::LogAction::Idle);
    CHECK(session.update(flying(kBaseUtc + 3, 200), 3999) == flight::LogAction::Idle);
    CHECK(session.update(flying(kBaseUtc + 4, 200), 4000) == flight::LogAction::AppendRecord);
}

TEST_CASE("log session: the file opens before the criterion agreed, so the roll is in it") {
    flight::LogSession session;
    uint32_t now_ms = 0;
    // Half a minute of taxiing, sampled and held in RAM.
    for (uint32_t i = 0; i < 8; i++, now_ms += 4000)
        session.update(parked(kBaseUtc + i * 4), now_ms);
    REQUIRE_FALSE(session.open());

    CHECK(session.update(flying(kBaseUtc + 32, 200), now_ms) == flight::LogAction::OpenSession);
    CHECK(session.open());
    // The session is named for the oldest sample still held, not for the instant
    // core/flight finally said "airborne" - 28 seconds of ground roll earlier.
    CHECK(session.session_id() == kBaseUtc + 4);
    CHECK(session.queued() == flight::kLogPreTakeoffRecords);

    flight::LogRecord first{};
    REQUIRE(session.take(first));
    CHECK(first.utc == kBaseUtc + 4);
    CHECK(first.flight_state == static_cast<uint8_t>(flight::FlightState::OnGround));
}

TEST_CASE("log session: a landing closes the session with a record that says so") {
    flight::LogSession session;
    uint32_t now_ms = 0;
    REQUIRE(session.update(flying(kBaseUtc, 200), now_ms) == flight::LogAction::OpenSession);
    flight::LogRecord drained{};
    while (session.take(drained)) {
    }

    now_ms += 4000;
    CHECK(session.update(parked(kBaseUtc + 4), now_ms) == flight::LogAction::CloseSession);
    CHECK_FALSE(session.open());
    REQUIRE(session.take(drained));
    CHECK(drained.session_end);
    CHECK(drained.utc == kBaseUtc + 4);
}

TEST_CASE("log session: a fix outage does not end the flight, and nothing is written across it") {
    flight::LogSession session;
    REQUIRE(session.update(flying(kBaseUtc, 200), 0) == flight::LogAction::OpenSession);

    messages::OwnState blind = flying(kBaseUtc + 4, 200);
    blind.fix_valid = false;
    CHECK(session.update(blind, 4000) == flight::LogAction::Idle);
    CHECK(session.open());

    // A row of zeroes is worse than a gap, so the gap is what the file gets.
    CHECK(session.update(flying(kBaseUtc + 8, 200), 8000) == flight::LogAction::AppendRecord);
    CHECK(session.session_id() == kBaseUtc);
}

TEST_CASE("log session: a writer that never drains loses the oldest, and says how many") {
    flight::LogSession session;
    for (uint32_t i = 0; i < 20; i++) session.update(flying(kBaseUtc + i * 4, 200), i * 4000);
    CHECK(session.queued() == flight::kLogPreTakeoffRecords);
    CHECK(session.dropped() == 20 - flight::kLogPreTakeoffRecords);
}

TEST_CASE("log ring: the first sector of a virgin partition is sector zero") {
    flight::LogRing ring;
    ring.configure(4, 170);
    CHECK_FALSE(ring.claimed());

    ring.claim_next_sector();
    CHECK(ring.claimed());
    CHECK(ring.sector() == 0);
    CHECK(ring.sequence() == 1);
    CHECK(ring.slot() == 0);
    CHECK_FALSE(ring.sector_exhausted());
}

TEST_CASE("log ring: the partition wraps and the sequence does not") {
    flight::LogRing ring;
    ring.configure(3, 2);
    for (int i = 0; i < 3; i++) ring.claim_next_sector();
    CHECK(ring.sector() == 2);
    CHECK(ring.sequence() == 3);

    // The oldest sector is reused, but its label is newer than everything else,
    // which is exactly how a boot finds the frontier again.
    ring.claim_next_sector();
    CHECK(ring.sector() == 0);
    CHECK(ring.sequence() == 4);

    ring.took_slot();
    CHECK_FALSE(ring.sector_exhausted());
    ring.took_slot();
    CHECK(ring.sector_exhausted());
}

TEST_CASE("log ring: recovery puts it back where the power cut left it") {
    flight::LogRing ring;
    ring.configure(330, 170);
    ring.restore(41, 170, 4242);
    CHECK(ring.sector_exhausted());
    ring.claim_next_sector();
    CHECK(ring.sector() == 42);
    CHECK(ring.sequence() == 4243);

    ring.rewind();
    CHECK(ring.sequence() == 0);
    ring.claim_next_sector();
    CHECK(ring.sector() == 0);
}

TEST_CASE("log link: the three commands, and nothing else") {
    auto framed = [](const char* json) {
        messages::RxFrame frame{};
        frame.endpoint = messages::Endpoint::Log;
        frame.len = static_cast<uint16_t>(std::strlen(json));
        std::memcpy(frame.data.data(), json, frame.len);
        return frame;
    };

    CHECK(comms::parse_log_request(framed("{\"cmd\":\"list\"}")).command ==
          comms::LogCommand::List);
    CHECK(comms::parse_log_request(framed("{\"cmd\":\"erase\"}")).command ==
          comms::LogCommand::Erase);

    const comms::LogRequest read =
        comms::parse_log_request(framed("{\"cmd\":\"read\",\"session\":1785628800,\"from\":170}"));
    CHECK(read.command == comms::LogCommand::Read);
    CHECK(read.session == 1785628800u);
    CHECK(read.from == 170u);

    // A read with no session names nothing, so it is not a read.
    CHECK_FALSE(comms::parse_log_request(framed("{\"cmd\":\"read\"}")).understood);
    CHECK_FALSE(comms::parse_log_request(framed("{\"cmd\":\"format\"}")).understood);

    // Config frames belong to the config service even if they reach this parser.
    messages::RxFrame wrong = framed("{\"cmd\":\"list\"}");
    wrong.endpoint = messages::Endpoint::Config;
    CHECK_FALSE(comms::parse_log_request(wrong).understood);
}

TEST_CASE("log link: base64 carries the raw record, padding and all") {
    char out[16];
    CHECK(comms::base64_encode(reinterpret_cast<const uint8_t*>("Man"), 3, out, sizeof(out)) == 4);
    CHECK(std::strcmp(out, "TWFu") == 0);
    CHECK(comms::base64_encode(reinterpret_cast<const uint8_t*>("Ma"), 2, out, sizeof(out)) == 4);
    CHECK(std::strcmp(out, "TWE=") == 0);
    CHECK(comms::base64_encode(reinterpret_cast<const uint8_t*>("M"), 1, out, sizeof(out)) == 4);
    CHECK(std::strcmp(out, "TQ==") == 0);
    // It refuses rather than truncates: a half-encoded chunk decodes to garbage.
    uint8_t big[64] = {0};
    CHECK(comms::base64_encode(big, sizeof(big), out, sizeof(out)) == -1);
}

TEST_CASE("log link: how many records ride in a chunk follows the payload, not a guess") {
    // 24 raw bytes are exactly 32 base64 characters and the envelope at its
    // widest is 83, so this arithmetic is exact rather than an estimate.
    // Nothing fits in what BLE merely guarantees; an iPhone carries three; the
    // 247-byte MTU the old fixed five was aimed at still carries five; and a
    // phone that negotiates the whole L2CAP MTU is not short-changed.
    CHECK(comms::log_records_per_chunk(hal::kMinimumLinkPayload) == 0);
    CHECK(comms::log_records_per_chunk(comms::kSmallestSupportedPayload) == 3);
    CHECK(comms::log_records_per_chunk(244) == 5);
    CHECK(comms::log_records_per_chunk(495) == comms::kLogRecordsPerChunkMax);

    // And a chunk really does fit the frame it was sized for, at the widest
    // session id and record index the partition can produce.
    uint8_t raw[comms::kLogChunkRawBytes];
    for (size_t i = 0; i < sizeof(raw); i++) raw[i] = static_cast<uint8_t>(0xA0 + i);
    const int payloads[3] = {comms::kSmallestSupportedPayload, 244, 495};
    for (int payload : payloads) {
        const int records = comms::log_records_per_chunk(payload);
        char buf[comms::kLogReplyCap];
        const int len =
            comms::format_log_chunk(buf, payload + 1, 4294967295u, 4294967295u, raw, records, true);
        CHECK(len > 0);
        CHECK(len <= payload);
        CHECK(std::strstr(buf, "\"cmd\":\"chunk\"") != nullptr);
        CHECK(std::strstr(buf, "\"eof\":true") != nullptr);
    }
}

TEST_CASE("log link: a reply that will not fit the frame is refused, never shortened") {
    // The writer leaves out a field that will not fit whole, so a cap too small
    // yields a short but perfectly valid object - a chunk with no "data" key, or
    // a session line with no record count. A tablet must not be handed either.
    uint8_t raw[comms::kLogChunkRawBytes] = {0};
    char buf[comms::kLogReplyCap];
    const int tiny = hal::kMinimumLinkPayload + 1;
    CHECK(comms::format_log_chunk(buf, tiny, 1785628800u, 0, raw, 1, false) == 0);
    CHECK(comms::format_log_session(buf, tiny, 0, 3, 1785628800u, 1700, false) == 0);
    CHECK(comms::format_log_count(buf, tiny, 3, false) == 0);
}

TEST_CASE("log link: the count comes first and says whether it is the whole truth") {
    char buf[comms::kLogReplyCap];
    CHECK(comms::format_log_count(buf, sizeof(buf), 3, false) > 0);
    CHECK(std::strstr(buf, "\"sessions\":3") != nullptr);
    CHECK(std::strstr(buf, "\"truncated\":false") != nullptr);
}

TEST_CASE("log link: a session line says how many records and whether the flight ended") {
    char buf[comms::kLogReplyCap];
    const int len = comms::format_log_session(buf, sizeof(buf), 0, 3, 1785628800u, 1700, false);
    CHECK(len > 0);
    CHECK(std::strstr(buf, "\"records\":1700") != nullptr);
    // The one thing a tablet must not hide: this flight stops where the power did.
    CHECK(std::strstr(buf, "\"closed\":false") != nullptr);
}
