// What the part asks for so that it survives: the current limit, DS 15, and the silent failures.
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/parts/sx1262/sx1262.h"

using namespace skyblip;
using namespace skyblip::parts;

static Sx1262 make(models::Sx1262& f) { return Sx1262(f, f, f.busy_pin, f.reset_pin, f.dio1_pin); }

TEST_CASE("radio: SetPaConfig widens the current limit to 140 mA and the driver takes it back") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    CHECK(chip.ocp == models::Sx1262::kOcpReset);

    REQUIRE(r.configure_radio(RadioConfig{}) == Status::Ok);
    CHECK(chip.ocp == sx::kOcpLimit);
    CHECK(chip.ocp < models::Sx1262::kOcpAfterPaConfigSx1262);
    CHECK(chip.ocp == 48);  // 120 mA in the register's own 2.5 mA steps
}

// Under what +14 dBm draws through this PA configuration, the chip backs the power off.
TEST_CASE("radio: the current limit clears the 90 mA that +14 dBm costs, under Semtech's 140") {
    CHECK(sx::kOcpLimitMa > 90);
    CHECK(sx::kOcpLimitMa < 140);
    CHECK(sx::kOcpLimit * 5 / 2 == sx::kOcpLimitMa);
}

// Every dwell reconfigures, and every reconfiguration issues the SetPaConfig that widens it again.
TEST_CASE("radio: the current limit survives every dwell, not only the first") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    RadioConfig cfg{};
    for (uint32_t hz : {868200000u, 868400000u, 868200000u}) {
        cfg.freq_hz = hz;
        REQUIRE(r.configure_radio(cfg) == Status::Ok);
        CAPTURE(hz);
        CHECK(chip.ocp == sx::kOcpLimit);
    }
}

// DS 15.2: the reset threshold costs 5 to 6 dB into any antenna that is wet, detuned or shadowed.
TEST_CASE("radio: the PA clamp is widened out of reset, and only its own four bits are touched") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    CHECK(chip.tx_clamp == models::Sx1262::kTxClampReset);
    REQUIRE(r.begin() == Status::Ok);
    CHECK((chip.tx_clamp & sx::kTxClampWidenBits) == sx::kTxClampWidenBits);
    // Bits 7-5 and 0 are the part's own.
    CHECK((chip.tx_clamp & 0xE1) == (models::Sx1262::kTxClampReset & 0xE1));
}

// DS 15.1.2 asks for this bit on any (G)FSK configuration, and every burst here is GFSK.
TEST_CASE("radio: the GFSK modulation-quality bit is set before anything is transmitted") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    CHECK((chip.tx_modulation & sx::kTxModulationGfskBit) == 0);
    REQUIRE(r.configure_radio(RadioConfig{}) == Status::Ok);
    CHECK((chip.tx_modulation & sx::kTxModulationGfskBit) != 0);
    CHECK((chip.tx_modulation & 0x01) == (models::Sx1262::kTxModulationReset & 0x01));
}

TEST_CASE("radio: a dwell reprogrammed on the other band keeps the modulation-quality bit") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    RadioConfig cfg{};
    cfg.freq_hz = 868400000;
    cfg.gaussian_bt_e2 = 50;
    REQUIRE(r.configure_radio(cfg) == Status::Ok);
    CHECK((chip.tx_modulation & sx::kTxModulationGfskBit) != 0);
}

// DS 13.1.2 caution: the dwell thread can be armed the instant after it parks the radio.
TEST_CASE("radio: waking straight after sleeping does not interrupt the configuration save") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    r.configure_radio(RadioConfig{});
    r.start_receive();
    r.sleep();
    REQUIRE(chip.sleeping);
    REQUIRE(r.wake() == Status::Ok);
    CHECK(chip.wakes == 1);
    CHECK(chip.fault == models::Sx1262::Fault::None);
    CHECK(chip.faults == 0);
}

TEST_CASE("radio: the model catches an NSS edge that arrives inside the save window") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    r.configure_radio(RadioConfig{});
    chip.select(true);
    chip.select(false);
    CHECK(chip.fault == models::Sx1262::Fault::None);

    r.sleep();
    chip.sleep_settle_spins = 0;
    chip.select(true);
    CHECK(chip.fault == models::Sx1262::Fault::SpiBeforeSleepSettled);
}

// DS 13.1.1: SetSleep is STDBY-only, and the dwell that just ended left the part receiving.
TEST_CASE("radio: the part is parked in standby before it is told to sleep") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    r.begin();
    r.configure_radio(RadioConfig{});
    r.start_receive();
    REQUIRE(chip.receiving);

    r.sleep();
    CHECK(chip.fault == models::Sx1262::Fault::None);
    CHECK(chip.sleeping);
    CHECK_FALSE(chip.receiving);
    CHECK(r.mode() == RadioMode::Sleep);
}

// DS 13.3.6: a TCXO part always raises XOSC_START_ERR at POR, and the host is told to clear it.
TEST_CASE("radio: the TCXO's expected start-up error is not read as a radio that failed") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    CHECK(r.begin() == Status::Ok);
    CHECK(r.device_errors() == 0);
}

// DS 8: "when BUSY is high, the host controller must wait until it goes down again".
TEST_CASE("radio: no command is put on the bus while the part is still busy with the last one") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(RadioConfig{}) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);
    const uint8_t frame[4] = {1, 2, 3, 4};
    REQUIRE(r.transmit(frame, sizeof(frame)) == Status::Ok);
    CHECK(chip.fault == models::Sx1262::Fault::None);
    CHECK(chip.faults == 0);
}

// A failed calibration has no symptom but a deaf radio, and deafness is cured by reinit, forever.
TEST_CASE("radio: a bring-up whose calibration failed reports the failing block, not silence") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    chip.fail_calibration = sx::kErrPllCalib;
    CHECK(r.begin() == Status::Down);
    CHECK((r.device_errors() & sx::kErrPllCalib) != 0);
    CHECK(chip.device_errors == 0);  // cleared, so the next bring-up answers for itself
}

TEST_CASE("radio: an image calibration that failed is not a radio that came up") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    chip.fail_calibration = sx::kErrImageCalib | sx::kErrPllLock;
    CHECK(r.begin() == Status::Down);
    CHECK((r.device_errors() & sx::kErrImageCalib) != 0);
    CHECK((r.device_errors() & sx::kErrPllLock) != 0);
}

// The clock really not appearing is not the flag the TCXO raises for free at power-on.
TEST_CASE("radio: an XOSC that never starts is still caught, once the free flag is cleared") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    chip.fail_calibration = sx::kErrXoscStart;
    CHECK(r.begin() == Status::Down);
    CHECK((r.device_errors() & sx::kErrXoscStart) != 0);
}

TEST_CASE("radio: a healthy bring-up reports no device error and says so") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    CHECK(r.begin() == Status::Ok);
    CHECK(r.device_errors() == 0);
    CHECK(chip.saw_cmd(sx::kGetDeviceErrors));
}

// The bits above PA_RAMP_ERR are RFU, and a part that floats them has not failed a calibration.
TEST_CASE("radio: reserved OpError bits are not read as a failure") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    chip.fail_calibration = static_cast<uint16_t>(~sx::kDeviceErrorMask);
    CHECK(r.begin() == Status::Ok);
    CHECK(r.device_errors() == 0);
}
