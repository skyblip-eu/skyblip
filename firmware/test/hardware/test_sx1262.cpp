// SX1262 driver recovery tests against models/sx1262.h with fault injection
// The class of intermittent bug that is hell to reproduce on hardware.
#include "doctest/doctest.h"
#include "core/protocol/adsl_uplink.h"
#include "core/protocol/air.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/parts/sx1262/sx1262.h"

using namespace skyblip;
using namespace skyblip::parts;

static Sx1262 make(models::Sx1262& f) { return Sx1262(f, f, f.busy_pin, f.reset_pin, f.dio1_pin); }

TEST_CASE("radio: begin configures the TCXO (DIO3) and RF switch (DIO2) — T-Echo wiring") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    CHECK(r.begin() == Status::Ok);
    // Without these two the SX1262 has no clock / no antenna path on this board.
    CHECK(chip.saw_cmd(sx::kSetDio2AsRfSwitch));
    CHECK(chip.saw_cmd(sx::kSetDio3AsTcxoCtrl));
    CHECK(chip.saw_cmd(sx::kCalibrate));
}

TEST_CASE("radio: begin + configure + receive brings the modem to Rx") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    CHECK(r.begin() == Status::Ok);
    CHECK(r.mode() == RadioMode::Standby);
    CHECK(r.configure_mband(MbandConfig{}) == Status::Ok);
    CHECK(r.start_receive() == Status::Ok);
    CHECK(r.mode() == RadioMode::Rx);
    CHECK(chip.reset_pulses >= 1);
}

TEST_CASE("radio: a queued RX packet is delivered via poll") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    r.configure_mband(MbandConfig{});
    r.start_receive();
    uint8_t pkt[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    chip.queue_rx(pkt, 8);
    uint8_t buf[32];
    RadioEvent ev = r.poll(buf, sizeof(buf));
    CHECK(ev.type == RadioEventType::RxDone);
    CHECK(int(ev.len) == 8);
    CHECK(buf[0] == 0x11);
    CHECK(buf[7] == 0x88);
}

TEST_CASE("radio: a CRC-error RX is reported as CrcError, NOT delivered") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    r.configure_mband(MbandConfig{});
    r.start_receive();
    uint8_t pkt[4] = {1, 2, 3, 4};
    chip.queue_rx(pkt, 4, /*crc_error=*/true);
    uint8_t buf[32];
    RadioEvent ev = r.poll(buf, sizeof(buf));
    CHECK(ev.type == RadioEventType::CrcError);
}

TEST_CASE("radio: BUSY stuck high is surfaced as a fault, not a hang") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    r.configure_mband(MbandConfig{});
    r.start_receive();
    chip.busy_stuck = true;
    uint8_t buf[32];
    RadioEvent ev = r.poll(buf, sizeof(buf));
    CHECK(ev.type == RadioEventType::Fault);
}

TEST_CASE("radio: health watchdog reinitialises after no-RX timeout (no-RX-in-N reinit, tested)") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    r.configure_mband(MbandConfig{});
    r.start_receive();
    CHECK(r.reinit_count() == 0);
    // 29 s: no reinit yet
    CHECK_FALSE(r.service(29000, 30000));
    // cross 30 s -> reinit
    CHECK(r.service(2000, 30000));
    CHECK(r.reinit_count() == 1);
    CHECK(r.mode() == RadioMode::Rx);  // back to receiving
    // a received packet resets the staleness timer
    uint8_t pkt[2] = {0xAB, 0xCD};
    chip.queue_rx(pkt, 2);
    uint8_t buf[8];
    r.poll(buf, sizeof(buf));
    CHECK_FALSE(r.service(29000, 30000));
}

TEST_CASE("radio: the tuned channel is what the PLL word resolves back to") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    // Both ADS-L M-band channels, 200 kHz apart (SRD-860 issue 2 C.2).
    MbandConfig cfg{};
    cfg.freq_hz = 868200000;
    r.configure_mband(cfg);
    CHECK(chip.freq_hz > 868199000);
    CHECK(chip.freq_hz < 868201000);
    cfg.freq_hz = 868400000;
    r.configure_mband(cfg);
    CHECK(chip.freq_hz > 868399000);
    CHECK(chip.freq_hz < 868401000);
}

TEST_CASE("radio: carrier sense reads the level on the tuned channel") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    r.configure_mband(MbandConfig{});
    r.start_receive();
    chip.rssi_dbm = -110;
    CHECK(r.rssi_inst() == -110);
    chip.rssi_dbm = -48;
    CHECK(r.rssi_inst() == -48);
}

TEST_CASE("radio: a delivered packet carries the level it arrived with") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    r.configure_mband(MbandConfig{});
    r.start_receive();
    uint8_t pkt[4] = {1, 2, 3, 4};
    chip.queue_rx(pkt, 4, /*crc_error=*/false, /*rssi=*/-73);
    uint8_t buf[8];
    const RadioEvent ev = r.poll(buf, sizeof(buf));
    CHECK(ev.type == RadioEventType::RxDone);
    CHECK(ev.rssi_dbm == -73);
}

TEST_CASE("radio: what was written to the buffer is what goes on air") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    r.configure_mband(MbandConfig{});
    const uint8_t frame[5] = {0x72, 0x4B, 0x18, 0xAA, 0x55};
    r.transmit(frame, sizeof(frame));
    CHECK_FALSE(chip.receiving);
    uint8_t out[8] = {0};
    uint8_t len = 0;
    REQUIRE(chip.take_tx(out, len));
    CHECK(len == sizeof(frame));
    CHECK(out[0] == 0x72);
    CHECK(out[4] == 0x55);
    CHECK_FALSE(chip.take_tx(out, len));
}

// The dwell hands the radio a sync window and a length; without them programmed
// the chip frames nothing, so this is where the shared-window trick either
// reaches the hardware or quietly does not.
TEST_CASE("radio: configure programs the sync window and the fixed read length") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    MbandConfig cfg{};
    cfg.sync = protocol::kSharedSync;
    cfg.sync_bits = protocol::kSharedSyncBits;
    cfg.payload_bytes = protocol::kRxChipBytes;
    CHECK(r.configure_mband(cfg) == Status::Ok);
    CHECK(chip.saw_cmd(sx::kWriteRegister));
    CHECK(chip.saw_cmd(sx::kSetPacketParams));
    CHECK(chip.sync[0] == protocol::kSharedSync[0]);
    CHECK(chip.sync[1] == protocol::kSharedSync[1]);
    CHECK(chip.sync_bits == protocol::kSharedSyncBits);
    CHECK(chip.payload_bytes == protocol::kRxChipBytes);
}

TEST_CASE("radio: a burst is framed from the chips after the sync window, either system") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    MbandConfig cfg{};
    cfg.sync = protocol::kSharedSync;
    cfg.sync_bits = protocol::kSharedSyncBits;
    cfg.payload_bytes = protocol::kRxChipBytes;
    r.configure_mband(cfg);
    r.start_receive();

    for (uint32_t sync_word : {protocol::kAdslSyncWord, protocol::kAlptasSyncWord}) {
        uint8_t payload[protocol::kAlptasFrameBytes];
        for (uint8_t i = 0; i < sizeof(payload); i++) payload[i] = static_cast<uint8_t>(i + 1);
        uint8_t chips[protocol::kTxChipBytes] = {0};
        const size_t chip_len =
            protocol::encode_mband(sync_word, payload, sizeof(payload), chips);
        REQUIRE(chip.receive_air(chips, static_cast<uint8_t>(chip_len)));

        uint8_t buf[64];
        RadioEvent ev = r.poll(buf, sizeof(buf));
        REQUIRE(ev.type == RadioEventType::RxDone);
        protocol::Frame frame{};
        REQUIRE(protocol::receive_mband(buf, ev.len, frame));
        const bool adsl = sync_word == protocol::kAdslSyncWord;
        if (adsl) CHECK(frame.system == protocol::System::AdslDirect);
        if (!adsl) CHECK(frame.system == protocol::System::Alptas);
        CHECK(frame.data[0] == 1);
        r.start_receive();
    }
}

TEST_CASE("radio: a burst carrying a sync word the dwell is not armed for is not reported") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    MbandConfig cfg{};
    cfg.sync = protocol::kUplinkSync;
    cfg.sync_bits = protocol::kUplinkSyncBits;
    cfg.payload_bytes = protocol::kRxChipBytes;
    r.configure_mband(cfg);
    r.start_receive();

    uint8_t payload[protocol::kAdslFrameBytes] = {0};
    uint8_t chips[protocol::kTxChipBytes] = {0};
    const size_t chip_len =
        protocol::encode_mband(protocol::kAdslSyncWord, payload, sizeof(payload), chips);
    CHECK_FALSE(chip.receive_air(chips, static_cast<uint8_t>(chip_len)));
    uint8_t buf[64];
    CHECK(r.poll(buf, sizeof(buf)).type == RadioEventType::None);
}
