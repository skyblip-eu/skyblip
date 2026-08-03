// How the radio is tuned, and what it can then hear: the modulation programmed
// for each of the two bands we dwell on, what the PLL word says the radio is
// tuned to, what a carrier-sense assessment reads off that channel, and the
// level a delivered packet arrived with. Split out of test_sx1262.cpp to keep
// both files under 500 lines; the driver and its model are exercised through
// the same public surface either way.
#include "core/protocol/adsl_uplink.h"
#include "core/protocol/air.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/parts/sx1262/sx1262.h"

using namespace skyblip;
using namespace skyblip::parts;

static Sx1262 make(models::Sx1262& f) { return Sx1262(f, f, f.busy_pin, f.reset_pin, f.dio1_pin); }

TEST_CASE("radio: the tuned channel is what the PLL word resolves back to") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    // Both ADS-L M-band channels, 200 kHz apart (SRD-860 issue 2 C.2).
    RadioConfig cfg{};
    cfg.freq_hz = 868200000;
    r.configure_radio(cfg);
    CHECK(chip.freq_hz > 868199000);
    CHECK(chip.freq_hz < 868201000);
    cfg.freq_hz = 868400000;
    r.configure_radio(cfg);
    CHECK(chip.freq_hz > 868399000);
    CHECK(chip.freq_hz < 868401000);
}

TEST_CASE("radio: carrier sense reads the level on the tuned channel") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    r.configure_radio(RadioConfig{});
    r.start_receive();
    chip.rssi_dbm = -110;
    CHECK(r.rssi_inst() == -110);
    chip.rssi_dbm = -48;
    CHECK(r.rssi_inst() == -48);
}

TEST_CASE("radio: an assessment interval is a run of reads, each answering for its own instant") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    r.configure_radio(RadioConfig{});
    r.start_receive();

    const int8_t levels[4] = {-112, -112, -44, -112};
    chip.set_rssi_sequence(levels, 4);
    for (int pass = 0; pass < 3; pass++)
        for (int i = 0; i < 4; i++) {
            CAPTURE(pass);
            CAPTURE(i);
            CHECK(r.rssi_inst() == levels[i]);
        }

    // With no sequence loaded the chip answers one steady level, as before.
    chip.set_rssi_sequence(nullptr, 0);
    chip.rssi_dbm = -97;
    CHECK(r.rssi_inst() == -97);
    CHECK(r.rssi_inst() == -97);
}

TEST_CASE("radio: a delivered packet carries the level it arrived with") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    r.configure_radio(RadioConfig{});
    r.start_receive();
    uint8_t pkt[4] = {1, 2, 3, 4};
    chip.queue_rx(pkt, 4, /*crc_error=*/false, /*rssi=*/-73);
    uint8_t buf[8];
    const RadioEvent ev = r.poll(buf, sizeof(buf));
    CHECK(ev.type == RadioEventType::RxDone);
    CHECK(ev.rssi_dbm == -73);
}

TEST_CASE("radio: the modem is programmed for 100 kbps, 50 kHz deviation, 234.3 kHz, unshaped") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    RadioConfig cfg{};
    cfg.sync = protocol::kSharedSync;
    cfg.sync_bits = protocol::kSharedSyncBits;
    cfg.payload_bytes = protocol::kRxChipBytes;
    REQUIRE(r.configure_radio(cfg) == Status::Ok);
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

TEST_CASE("radio: the O band is programmed for 200 kbps GMSK in a 250 kHz channel") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    RadioConfig cfg{};
    cfg.freq_hz = 869525000;
    cfg.bitrate = protocol::kUplinkChipRateBps;
    cfg.fdev_hz = protocol::kUplinkDeviationHz;
    cfg.bandwidth_hz = protocol::kUplinkChannelBandwidthHz;
    cfg.gaussian_bt_e2 = protocol::kUplinkGaussianBtE2;
    cfg.sync = protocol::kUplinkSync;
    cfg.sync_bits = protocol::kUplinkSyncBits;
    cfg.payload_bytes = protocol::kUplinkFrameBytes;
    REQUIRE(r.configure_radio(cfg) == Status::Ok);
    CHECK(chip.bitrate == 200000);
    CHECK(chip.fdev_hz > 49990);
    CHECK(chip.fdev_hz < 50010);
    CHECK(chip.pulse_shape == sx::kGaussianBt0p5);
    // The narrowest entry of DS 13.4.6's table that still passes 250 kHz. The
    // one below it, 234.3 kHz, clips the channel.
    CHECK(chip.rx_bandwidth == 0x19);
    CHECK(chip.payload_bytes == protocol::kUplinkFrameBytes);
    // A whole RS(255,223) codeword is what this band reads, and the packet
    // handler's length field is exactly wide enough for it.
    CHECK(chip.payload_bytes == 255);
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
