// The flight log on the running product: the same board, the same service list,
// the same part models the silicon build uses, over a flash fake that keeps
// NOR's two awkward truths and knows how to die mid-program.
#include <string>

#include "core/flight/log_record.h"
#include "doctest/doctest.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

// The tablet's side of the transfer, and the only decoder in the tree: the
// device never speaks IGC, it hands over the raw records and the CRC that came
// off the flash with them.
int base64_decode(const std::string& in, uint8_t* out, int cap) {
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    int n = 0;
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : in) {
        const int v = value(c);
        if (v < 0) continue;
        buffer = (buffer << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits < 8) continue;
        bits -= 8;
        if (n < cap) out[n++] = static_cast<uint8_t>((buffer >> bits) & 0xFF);
    }
    return n;
}

std::string field(const std::string& json, const char* key) {
    const std::string needle = std::string("\"") + key + "\":";
    const size_t at = json.find(needle);
    if (at == std::string::npos) return "";
    size_t start = at + needle.size();
    if (json[start] == '"') {
        const size_t end = json.find('"', start + 1);
        return json.substr(start + 1, end - start - 1);
    }
    const size_t end = json.find_first_of(",}", start);
    return json.substr(start, end - start);
}

const platform::host::Link::Frame* last_log_frame(Rig& rig) {
    for (size_t i = rig.platform.link().sent.size(); i > 0; i--) {
        const auto& frame = rig.platform.link().sent[i - 1];
        if (frame.endpoint == messages::Endpoint::Log) return &frame;
    }
    return nullptr;
}

// The tablet's two-step list: how many flights, then one line each.
std::string list_session(Rig& rig, uint32_t& t, uint32_t index) {
    char command[64];
    std::snprintf(command, sizeof(command), "{\"cmd\":\"list\",\"index\":%u}", index);
    rig.platform.link().clear();
    rig.send_log(command);
    rig.run(t, t + 200);
    t += 200;
    const platform::host::Link::Frame* frame = last_log_frame(rig);
    return frame == nullptr ? "" : frame->bytes;
}

std::string list_count(Rig& rig, uint32_t& t) {
    rig.platform.link().clear();
    rig.send_log("{\"cmd\":\"list\"}");
    rig.run(t, t + 200);
    t += 200;
    const platform::host::Link::Frame* frame = last_log_frame(rig);
    return frame == nullptr ? "" : frame->bytes;
}

// A takeoff, some minutes of flight, and a landing, in the units core/flight
// judges. 50 m/s is 200 quarter-metres per second; standing still is zero.
void taxi(Rig& rig, uint32_t& t, uint32_t seconds) { rig.seconds(t, seconds, 0, 300); }
void fly(Rig& rig, uint32_t& t, uint32_t seconds) { rig.seconds(t, seconds, 200, 800); }

}  // namespace

TEST_CASE("flight log: a device parked on the ground writes nothing at all") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    const uint32_t erases_at_boot = rig.platform.log_flash().erases;
    uint32_t t = 0;

    taxi(rig, t, 120);

    CHECK_FALSE(rig.product.flight_log().recording());
    CHECK(rig.product.flight_log().records_written() == 0);
    CHECK(rig.platform.log_flash().writes == 0);
    CHECK(rig.platform.log_flash().erases == erases_at_boot);
}

TEST_CASE("flight log: a takeoff opens a session, a landing closes it, the ground after is quiet") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;

    taxi(rig, t, 40);
    REQUIRE_FALSE(rig.product.flight_log().recording());

    fly(rig, t, 120);
    CHECK(rig.product.flight_log().recording());
    // The session is named for a sample taken before core/flight would commit
    // to the takeoff, so the ground roll is in the file.
    CHECK(rig.product.flight_log().session_id() < Rig::kUtcBase + 45);
    const uint32_t airborne_records = rig.product.flight_log().records_written();
    // Two minutes at a record every four seconds, plus the pre-takeoff ring.
    CHECK(airborne_records >= 30);

    taxi(rig, t, 40);
    CHECK_FALSE(rig.product.flight_log().recording());
    const uint32_t after_landing = rig.product.flight_log().records_written();
    // core/flight needs ten seconds of stillness before it will call it a
    // landing, and those seconds are part of the flight, so they are logged;
    // then one last record marks the end.
    CHECK(after_landing > airborne_records);

    // And then nothing: the ring goes back to being a holding pen, and nothing
    // the writer owed was lost on the way.
    taxi(rig, t, 60);
    CHECK(rig.product.flight_log().records_written() == after_landing);
    CHECK(rig.product.flight_log().records_dropped() == 0);
    CHECK(rig.product.flight_log().sessions_on_flash() == 1);
}

TEST_CASE("flight log: a committed record survives the cell dying, and the torn one is not a fix") {
    Rig flight;
    REQUIRE(flight.setup() == Status::Ok);
    uint32_t t = 0;
    taxi(flight, t, 20);
    fly(flight, t, 80);
    const uint32_t committed = flight.product.flight_log().records_written();
    REQUIRE(committed > 8);

    // The cell gives out nine bytes into the next record. Everything before it
    // is on the part; that record is half a record.
    flight.platform.log_flash().cut_power_after(9);
    fly(flight, t, 20);
    REQUIRE(flight.platform.log_flash().dead());
    CHECK(flight.product.flight_log().records_written() == committed);

    // A reboot is a new device handed the same bytes.
    Rig rebooted;
    rebooted.platform.log_flash().restore(flight.platform.log_flash().bytes());
    REQUIRE(rebooted.setup() == Status::Ok);

    uint32_t rt = 0;
    taxi(rebooted, rt, 20);
    CHECK(field(list_count(rebooted, rt), "sessions") == "1");

    const std::string listed = list_session(rebooted, rt, 0);
    // Every record that was committed is still offered, and the half-written one
    // is not counted: it is never read back as a position.
    CHECK(field(listed, "records") == std::to_string(committed));
    // The flight stops where the power did, and the tablet is told so rather
    // than shown a truncated flight as a complete one.
    CHECK(field(listed, "closed") == "false");
    CHECK(field(listed, "session") == std::to_string(flight.product.flight_log().session_id()));
}

TEST_CASE("flight log: the next flight after a power cut does not overwrite the last one") {
    Rig flight;
    REQUIRE(flight.setup() == Status::Ok);
    uint32_t t = 0;
    taxi(flight, t, 20);
    fly(flight, t, 60);
    const uint32_t first_session = flight.product.flight_log().session_id();
    const uint32_t committed = flight.product.flight_log().records_written();

    Rig rebooted;
    rebooted.platform.log_flash().restore(flight.platform.log_flash().bytes());
    REQUIRE(rebooted.setup() == Status::Ok);
    // The device came back up ten minutes later, which is what makes the second
    // session a second session and not a continuation of the first.
    rebooted.utc_offset_s = 600;
    uint32_t rt = 0;
    taxi(rebooted, rt, 20);
    fly(rebooted, rt, 60);
    taxi(rebooted, rt, 40);
    CHECK(rebooted.product.flight_log().session_id() != first_session);

    CHECK(field(list_count(rebooted, rt), "sessions") == "2");

    bool found_first = false;
    for (uint32_t i = 0; i < 2; i++) {
        const std::string line = list_session(rebooted, rt, i);
        REQUIRE(line.find("\"cmd\":\"session\"") != std::string::npos);
        if (field(line, "session") != std::to_string(first_session)) continue;
        found_first = true;
        // The flight that was on the part before the reboot is untouched by the
        // one flown after it.
        CHECK(field(line, "records") == std::to_string(committed));
    }
    CHECK(found_first);
}

TEST_CASE("flight log: the write frontier is found from the labels, not by reading the partition") {
    Rig flight;
    REQUIRE(flight.setup() == Status::Ok);
    uint32_t t = 0;
    taxi(flight, t, 20);
    fly(flight, t, 60);

    Rig rebooted;
    rebooted.platform.log_flash().restore(flight.platform.log_flash().bytes());
    REQUIRE(rebooted.setup() == Status::Ok);

    const uint32_t partition =
        platform::host::FlashRegion::kSectorBytes * platform::host::FlashRegion::kSectorCount;
    // One 16-byte label per sector: 5280 bytes of a 1.29 MB partition, which is
    // the difference between a boot that is instant and a boot that is a
    // quarter of a second of SPI.
    CHECK(rebooted.product.flight_log().recovery_bytes_read() ==
          flight::kLogSectorHeaderBytes * platform::host::FlashRegion::kSectorCount);
    CHECK(rebooted.product.flight_log().recovery_bytes_read() * 200 < partition);
}

TEST_CASE("flight log: the tablet lists a flight and reads it back five records at a time") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    taxi(rig, t, 20);
    fly(rig, t, 80);
    taxi(rig, t, 40);
    const uint32_t session = rig.product.flight_log().session_id();
    const uint32_t written = rig.product.flight_log().records_written();

    CHECK(field(list_count(rig, t), "sessions") == "1");
    const std::string listed = list_session(rig, t, 0);
    CHECK(field(listed, "records") == std::to_string(written));
    CHECK(field(listed, "closed") == "true");

    // The read is asked for one chunk at a time, carrying the index it wants:
    // the next command IS the acknowledgement, so a dropped connection resumes
    // by asking again.
    uint32_t from = 0;
    int chunks = 0;
    uint32_t decoded_records = 0;
    bool eof = false;
    uint32_t last_utc = 0;
    while (!eof && chunks < 200) {
        char command[96];
        std::snprintf(command, sizeof(command), "{\"cmd\":\"read\",\"session\":%u,\"from\":%u}",
                      session, from);
        rig.platform.link().clear();
        rig.send_log(command);
        rig.run(t, t + 100);
        t += 100;
        const auto* chunk = last_log_frame(rig);
        REQUIRE(chunk != nullptr);
        REQUIRE(chunk->bytes.find("\"cmd\":\"chunk\"") != std::string::npos);

        const int n = std::atoi(field(chunk->bytes, "n").c_str());
        CHECK(n <= comms::kLogRecordsPerChunk);
        uint8_t raw[comms::kLogChunkRawBytes];
        const int bytes = base64_decode(field(chunk->bytes, "data"), raw, sizeof(raw));
        CHECK(bytes == n * static_cast<int>(flight::kLogRecordBytes));

        for (int i = 0; i < n; i++) {
            flight::LogRecord record{};
            // Every record carries the checksum it was written with, so the
            // transfer is verified against the flash and not against the link.
            REQUIRE(flight::decode_log_record(raw + i * flight::kLogRecordBytes, session, record) ==
                    Status::Ok);
            CHECK(record.utc >= last_utc);
            last_utc = record.utc;
            decoded_records++;
        }
        from += static_cast<uint32_t>(n);
        eof = field(chunk->bytes, "eof") == "true";
        chunks++;
    }

    CHECK(eof);
    CHECK(decoded_records == written);
    // The last record of a flight that ended says so.
    CHECK(chunks == static_cast<int>((written + comms::kLogRecordsPerChunk - 1) /
                                     comms::kLogRecordsPerChunk));
}

TEST_CASE("flight log: an offload is refused in the air, on the same gate a settings change is") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    taxi(rig, t, 20);
    fly(rig, t, 60);
    REQUIRE(rig.product.config().config().flight_state() == comms::FlightState::Airborne);

    rig.platform.link().clear();
    rig.send_log("{\"cmd\":\"list\"}");
    rig.run(t, t + 200);
    t += 200;
    const auto* refused = last_log_frame(rig);
    REQUIRE(refused != nullptr);
    CHECK(refused->bytes.find("in_flight") != std::string::npos);

    rig.platform.link().clear();
    rig.send_log("{\"cmd\":\"erase\"}");
    rig.run(t, t + 200);
    t += 200;
    CHECK(last_log_frame(rig)->bytes.find("in_flight") != std::string::npos);
    CHECK(rig.product.config().config().pending() == comms::Pending::None);

    // And it stays refused: the log is still being written to.
    CHECK(rig.product.flight_log().recording());
}

TEST_CASE("flight log: erasing every flight takes the button, not just the phone") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    taxi(rig, t, 20);
    fly(rig, t, 60);
    taxi(rig, t, 40);
    REQUIRE(rig.product.flight_log().records_written() > 0);

    rig.send_log("{\"cmd\":\"erase\"}");
    rig.run(t, t + 200);
    t += 200;
    // The same prompt machine a firmware upload knocks on, so a pilot reads one
    // kind of authorisation on the glass and not two.
    CHECK(rig.product.config().config().pending() == comms::Pending::EraseLog);
    CHECK(rig.product.screen().prompt() == comms::Pending::EraseLog);
    CHECK(rig.product.flight_log().sessions_on_flash() >= 1);

    // A single press at a prompt refuses, which is what fail-closed means here.
    rig.press(t);
    rig.run(t, t + 1200);
    t += 1200;
    CHECK(rig.product.config().config().pending() == comms::Pending::None);
    CHECK_FALSE(rig.product.flight_log().erasing());

    rig.send_log("{\"cmd\":\"erase\"}");
    rig.run(t, t + 200);
    t += 200;
    REQUIRE(rig.product.config().config().pending() == comms::Pending::EraseLog);
    rig.double_press(t);
    rig.run(t, t + 100);
    t += 100;
    CHECK(rig.product.flight_log().erasing());

    // One sector a pass: 330 erases must not be one tick that stops reporting
    // progress for the better part of a minute.
    rig.platform.link().clear();
    rig.run(t, t + 40000, 50);
    t += 40000;
    CHECK_FALSE(rig.product.flight_log().erasing());
    CHECK(rig.product.flight_log().records_written() == 0);

    CHECK(field(list_count(rig, t), "sessions") == "0");
}

TEST_CASE("flight log: with no storage the device flies and logs nothing") {
    constexpr hal::Capabilities kNoStorage = static_cast<hal::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(hal::Capability::Storage));
    Rig rig{kNoStorage};
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.product.degraded() == hal::Capability::Storage);
    CHECK_FALSE(rig.product.flight_log().available());

    uint32_t t = 0;
    taxi(rig, t, 20);
    fly(rig, t, 60);
    // It still flies: the radio, the alarm and the panel know nothing about this.
    CHECK(rig.state().own.flight_state == static_cast<uint8_t>(flight::FlightState::Airborne));
    CHECK(rig.product.flight_log().records_written() == 0);

    taxi(rig, t, 40);
    rig.platform.link().clear();
    rig.send_log("{\"cmd\":\"list\"}");
    rig.run(t, t + 200);
    // And it says so, rather than answering an empty log.
    CHECK(last_log_frame(rig)->bytes.find("no_storage") != std::string::npos);
}
