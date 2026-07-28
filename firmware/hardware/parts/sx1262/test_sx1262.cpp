// SX1262 driver recovery tests against models/sx1262.h with fault injection
// The class of intermittent bug that is hell to reproduce on hardware.
#include "hardware/parts/sx1262/sx1262.h"
#include "hardware/parts/sx1262/model.h"
#include "doctest/doctest.h"

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
