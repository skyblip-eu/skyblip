// SX1262 driver recovery tests against models/sx1262.h with fault injection
// The class of intermittent bug that is hell to reproduce on hardware.
#include "core/protocol/adsl_uplink.h"
#include "core/protocol/air.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/parts/sx1262/sx1262.h"

using namespace skyblip;
using namespace skyblip::parts;

static Sx1262 make(models::Sx1262& f) { return Sx1262(f, f, f.busy_pin, f.reset_pin, f.dio1_pin); }

TEST_CASE("radio: begin configures the TCXO (DIO3) and RF switch (DIO2) for T-Echo wiring") {
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

// The dwell hands the radio a sync window and a length. Without them programmed
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
        const size_t chip_len = protocol::encode_mband(sync_word, payload, sizeof(payload), chips);
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

// A1. The modem is the one part of this chip that cannot be left at its reset
// defaults: MbandConfig carried a bitrate and a deviation nothing ever wrote.
TEST_CASE("radio: the modem is programmed for 100 kbps, 50 kHz deviation, 234.3 kHz, unshaped") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    MbandConfig cfg{};
    cfg.sync = protocol::kSharedSync;
    cfg.sync_bits = protocol::kSharedSyncBits;
    cfg.payload_bytes = protocol::kRxChipBytes;
    REQUIRE(r.configure_mband(cfg) == Status::Ok);
    CHECK(chip.modulation_set);
    CHECK(chip.bitrate == 100000);
    CHECK(chip.fdev_hz > 49990);
    CHECK(chip.fdev_hz < 50010);
    // DS 13.4.6 order: bit rate, pulse shape, RX bandwidth, deviation.
    CHECK(chip.modulation[3] == sx::kPulseShapeNone);
    CHECK(chip.modulation[4] == sx::kRxBandwidth234kHz);
    CHECK(chip.pulse_shape == sx::kPulseShapeNone);
    CHECK(chip.rx_bandwidth == sx::kRxBandwidth234kHz);
    // DS 13.1.4: the packet handler is configured after the modem, not before.
    CHECK(chip.cmd_order(sx::kSetRfFrequency) < chip.cmd_order(sx::kSetModulationParams));
    CHECK(chip.cmd_order(sx::kSetModulationParams) < chip.cmd_order(sx::kSetPacketParams));
}

TEST_CASE("radio: a modem left on its reset defaults frames nothing off the air") {
    models::Sx1262 chip;
    chip.sync_bits = protocol::kSharedSyncBits;
    chip.payload_bytes = protocol::kRxChipBytes;
    for (uint8_t i = 0; i < protocol::kSharedSyncBits / 8; i++)
        chip.sync[i] = protocol::kSharedSync[i];
    uint8_t payload[protocol::kAdslFrameBytes] = {0};
    uint8_t chips[protocol::kTxChipBytes] = {0};
    const size_t chip_len =
        protocol::encode_mband(protocol::kAdslSyncWord, payload, sizeof(payload), chips);
    CHECK_FALSE(chip.receive_air(chips, static_cast<uint8_t>(chip_len)));
}

// A2. IrqMask is 0x0000 out of reset and gates the status register, so an
// unprogrammed mask is a radio that reports neither RxDone nor TxDone, for ever.
TEST_CASE("radio: an IRQ bit that was never unmasked is never reported") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    r.configure_mband(MbandConfig{});
    uint8_t pkt[4] = {1, 2, 3, 4};
    chip.queue_rx(pkt, 4);
    uint8_t buf[32];
    CHECK(r.poll(buf, sizeof(buf)).type == RadioEventType::None);
    CHECK(r.start_receive() == Status::Ok);
    CHECK(r.poll(buf, sizeof(buf)).type == RadioEventType::RxDone);
}

TEST_CASE("radio: the IRQ mask and the DIO1 mask are programmed before the receiver is started") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    r.configure_mband(MbandConfig{});
    CHECK(chip.irq_mask == 0);
    r.start_receive();
    CHECK(chip.cmd_order(sx::kSetDioIrqParams) < chip.cmd_order(sx::kSetRx));
    CHECK((chip.irq_mask & sx::kIrqRxDone) != 0);
    CHECK((chip.irq_mask & sx::kIrqCrcErr) != 0);
    // DIO1 is the only interrupt line wired on this board.
    CHECK(chip.dio1_mask == chip.irq_mask);
    chip.queue_rx(nullptr, 0);
    CHECK(chip.get(chip.dio1_pin));
}

TEST_CASE("radio: the IRQ mask is programmed before the transmitter is keyed") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    r.configure_mband(MbandConfig{});
    const uint8_t frame[4] = {1, 2, 3, 4};
    REQUIRE(r.transmit(frame, sizeof(frame)) == Status::Ok);
    CHECK(chip.cmd_order(sx::kSetDioIrqParams) < chip.cmd_order(sx::kSetTx));
    CHECK((chip.irq_mask & sx::kIrqTxDone) != 0);
    CHECK((chip.irq_mask & sx::kIrqTimeout) != 0);
    chip.signal_tx_done();
    uint8_t buf[8];
    CHECK(r.poll(buf, sizeof(buf)).type == RadioEventType::TxDone);
}

// A3. Nothing in this tree set output power. The PA config is also the write
// that raises OCP, and the band's ceiling is a regulation, not a preference.
TEST_CASE("radio: the PA is the SX1262 high-power configuration, which is also OCP at 140 mA") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    r.configure_mband(MbandConfig{});
    CHECK(chip.pa_set);
    CHECK(chip.pa_config[0] == 0x04);
    CHECK(chip.pa_config[1] == 0x07);
    CHECK(chip.pa_config[2] == 0x00);
    CHECK(chip.pa_config[3] == 0x01);
    CHECK(chip.cmd_order(sx::kSetPaConfig) < chip.cmd_order(sx::kSetTxParams));
}

TEST_CASE("radio: output power is the 868 MHz SRD 25 mW e.r.p. ceiling, ramped over 200 us") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    r.configure_mband(MbandConfig{});
    CHECK(chip.tx_power_set);
    CHECK(chip.tx_power_dbm == sx::kSrd868ErpLimitDbm);
    CHECK(chip.tx_power_dbm == 14);  // 25 mW e.r.p., ERC 70-03 band h1.4
    CHECK(chip.ramp_time == sx::kRampTime200Us);
}

TEST_CASE("radio: a chip whose output power was never programmed refuses to transmit") {
    models::Sx1262 chip;
    const uint8_t frame[3] = {1, 2, 3};
    chip.select(true);
    chip.transfer(frame, nullptr, 1);
    chip.select(false);
    chip.select(true);
    const uint8_t tx[4] = {sx::kSetTx, 0, 0, 0};
    chip.transfer(tx, nullptr, sizeof(tx));
    chip.select(false);
    CHECK(chip.fault == models::Sx1262::Fault::TxWithoutPower);
    CHECK_FALSE(chip.tx_pending);
}

// A4. Image rejection is calibrated per band, and only against the clock the
// chip will actually run on.
TEST_CASE("radio: image rejection is calibrated for 863-870 MHz after the TCXO is up") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    CHECK(chip.image_calibrated);
    CHECK(chip.image_band[0] == 0xD7);
    CHECK(chip.image_band[1] == 0xDB);
    CHECK(chip.cmd_order(sx::kSetDio3AsTcxoCtrl) < chip.cmd_order(sx::kCalibrateImage));
    CHECK(chip.cmd_order(sx::kCalibrate) < chip.cmd_order(sx::kCalibrateImage));
    CHECK(chip.fault == models::Sx1262::Fault::None);
    // Once per band: the second dwell does not pay for it again.
    const int before = chip.cmd_order(sx::kCalibrateImage);
    r.configure_mband(MbandConfig{});
    CHECK(chip.cmd_order(sx::kCalibrateImage) == before);
}

// A5. The previous dwell leaves the chip in continuous RX, and SetRfFrequency
// and SetPacketType are standby-only commands.
TEST_CASE("radio: retuning a receiving radio is bracketed by standby and returns to RX") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    MbandConfig cfg{};
    cfg.freq_hz = 868200000;
    r.configure_mband(cfg);
    r.start_receive();
    REQUIRE(r.mode() == RadioMode::Rx);

    int standbys = 0;
    for (uint8_t c : chip.cmds_seen)
        if (c == sx::kSetStandby) standbys++;
    cfg.freq_hz = 868400000;
    REQUIRE(r.configure_mband(cfg) == Status::Ok);
    int after = 0;
    for (uint8_t c : chip.cmds_seen)
        if (c == sx::kSetStandby) after++;
    CHECK(after == standbys + 1);
    CHECK(chip.fault == models::Sx1262::Fault::None);
    CHECK(chip.freq_hz > 868399000);
    CHECK(r.mode() == RadioMode::Rx);
    CHECK(chip.receiving);
}

TEST_CASE("radio: a standby-only command issued while the receiver runs is a chip fault") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    r.configure_mband(MbandConfig{});
    r.start_receive();
    REQUIRE(chip.fault == models::Sx1262::Fault::None);
    const uint8_t retune[5] = {sx::kSetRfFrequency, 0x36, 0x40, 0x00, 0x00};
    chip.select(true);
    chip.transfer(retune, nullptr, sizeof(retune));
    chip.select(false);
    CHECK(chip.fault == models::Sx1262::Fault::ConfigOutsideStandby);
}

// A6. NRESET low for at least 100 us (DS 8.1). io::Gpio has no delay, so the
// pulse is a bounded spin and the model counts it.
TEST_CASE("radio: NRESET is held low for the datasheet minimum, and a shorter pulse is a fault") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    CHECK(chip.reset_pulses == 1);
    CHECK(chip.reset_low_spins >= sx::kResetLowSpins);
    CHECK(chip.fault == models::Sx1262::Fault::None);

    models::Sx1262 rushed;
    rushed.set(rushed.reset_pin, false);
    rushed.set(rushed.reset_pin, true);
    CHECK(rushed.fault == models::Sx1262::Fault::ShortReset);
}

// A7. SetTx with no timeout is a PA that stays keyed when TxDone never arrives.
TEST_CASE("radio: SetTx carries a timeout past the frame's air time at the configured bitrate") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    MbandConfig cfg{};
    cfg.sync = protocol::kSharedSync;
    cfg.sync_bits = protocol::kSharedSyncBits;
    cfg.payload_bytes = protocol::kRxChipBytes;
    r.configure_mband(cfg);
    uint8_t frame[protocol::kAdslFrameBytes] = {0};
    REQUIRE(r.transmit(frame, sizeof(frame)) == Status::Ok);
    CHECK(chip.tx_timeout_ticks != 0);
    // 100 kbps: one bit is 10 us. Preamble + sync window + payload.
    const uint32_t air_us =
        (sx::kPreambleChips + protocol::kSharedSyncBits + sizeof(frame) * 8u) * 10u;
    const uint32_t timeout_us = chip.tx_timeout_ticks * sx::kTimeoutStepNs / 1000u;
    CHECK(timeout_us > air_us);
    CHECK(timeout_us < air_us + 30000);
}

TEST_CASE("radio: a transmission that never completes is recovered to RX and counted") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    r.configure_mband(MbandConfig{});
    r.start_receive();
    const uint8_t frame[4] = {1, 2, 3, 4};
    REQUIRE(r.transmit(frame, sizeof(frame)) == Status::Ok);
    REQUIRE(r.mode() == RadioMode::Tx);
    CHECK(r.tx_recovery_count() == 0);
    REQUIRE(chip.expire_tx());  // TxDone never comes; the chip's timeout does
    uint8_t buf[32];
    CHECK(r.poll(buf, sizeof(buf)).type == RadioEventType::Timeout);
    CHECK(r.tx_recovery_count() == 1);
    CHECK(r.mode() == RadioMode::Rx);
    CHECK(chip.receiving);
}
