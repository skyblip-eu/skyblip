// devices/boards/t_echo_plus/pins.h — LilyGO T-Echo Plus (nRF52840) pin map.
#ifndef SKYBLIP_DEVICES_BOARDS_T_ECHO_PLUS_PINS_H
#define SKYBLIP_DEVICES_BOARDS_T_ECHO_PLUS_PINS_H

#include <cstdint>

namespace skyblip::board::t_echo_plus {

constexpr int kPinNum(int port, int pin) { return port * 32 + pin; }

constexpr int kGnssRx = kPinNum(1, 9);
constexpr int kGnssTx = kPinNum(1, 8);
constexpr int kGnssPps = kPinNum(1, 4);

constexpr int kRadioSs = kPinNum(0, 24);
constexpr int kRadioDio1 = kPinNum(0, 20);
constexpr int kRadioDio3 = kPinNum(0, 21);  // TCXO control @ 1.8V (SetDIO3AsTcxoCtrl)
constexpr int kRadioBusy = kPinNum(0, 17);
constexpr int kRadioRst = kPinNum(0, 25);
// SX1262 DIO2 is wired as the RF/antenna switch (SetDIO2AsRfSwitchCtrl).
constexpr bool kRadioDio2IsRfSwitch = true;

constexpr int kEpdMiso = kPinNum(1, 7);
constexpr int kEpdMosi = kPinNum(0, 29);
constexpr int kEpdSck = kPinNum(0, 31);
constexpr int kEpdSs = kPinNum(0, 30);
constexpr int kEpdDc = kPinNum(0, 28);
constexpr int kEpdRst = kPinNum(0, 2);
constexpr int kEpdBusy = kPinNum(0, 3);
constexpr int kEpdBacklight = kPinNum(1, 11);

// Sensor I2C. Three parts share it, and two of them are NOT guaranteed present:
// the barometer is "optional, selected" in the BOM and the IMU is a pluggable
// daughter module (LilyGO ships both T-BHI260 and T-ICM29048 into the same 5-pin
// header), so both are probed at runtime rather than assumed.
constexpr int kSda = kPinNum(0, 26);
constexpr int kScl = kPinNum(0, 27);

// BME280. Our BOM says 0x76; LilyGO's own README I2C table and SoftRF
// (platform/nRF52.h) both say 0x77. Nobody has settled it on a bench, so the
// devicetree declares BOTH and the board takes whichever answers.
constexpr uint8_t kBaroAddrPrimary = 0x76;
constexpr uint8_t kBaroAddrAlternate = 0x77;

// BHI260AP host interface. 0x29 if the module's U1 short-point is closed.
constexpr uint8_t kImuAddr = 0x28;
// The BHI260AP's HIRQ line is NOT connected on this board (LilyGO README I2C
// table lists it as NC), so the host must poll; never wait on an interrupt.
constexpr bool kImuHasIrq = false;

// The external 2 MB QSPI flash (MX25R1635F) sits on P1.12/13/14/15 + P0.05/0.07
// as a FOUR-LANE QSPI, not a 3-wire SPI. It is driven by Zephyr's
// nordic,qspi-nor via the qspi_default pinctrl group in the board devicetree,
// which is the single source of truth for those six pins - nothing here reads
// them, so they are deliberately not duplicated as constants. The old kSflMosi/
// kSflMiso/kSflSck names were a mis-transcription of a QSPI bus as an SPI one.

constexpr int kButton = kPinNum(1, 10);   // main button (active-low, pull-up)
constexpr int kButton2 = kPinNum(0, 18);  // 2nd button (reset-labelled, usable GPIO)
constexpr int kTouch = kPinNum(0, 11);    // capacitive touch pad

// Buzzer + vibration motor exist ONLY on the T-Echo *Plus*. Pins confirmed from
// the Meshtastic `t-echo-plus` variant: PIN_BUZZER P0.06, PIN_DRV_EN P0.08.
constexpr int kBuzzer = kPinNum(0, 6);  // piezo buzzer
constexpr int kVibro = kPinNum(0, 8);   // DRV haptic / vibration motor enable

// Power gating: the SX1262 radio, the e-paper AND (on REV_2 and later, which
// includes the Plus) the external QSPI flash sit behind PIN_POWER_EN — SoftRF's
// iomap/LilyGO_TEcho.h:80 lists "REV_2: FLASH, GNSS, SENSOR" against this pin.
// It MUST be driven high at boot or the radio SPI is dead, the EPD won't respond
// and MCUboot cannot read the secondary image slot. #1 bring-up gotcha.
//
// Asserted in boards/lilygo/t_echo_plus/board.c, at board level rather than in
// the application, so the bootloader gets it too. Listed here for the pin map's
// completeness; nothing in the application drives these.
constexpr int kIoPwr = kPinNum(0, 12);          // PIN_POWER_EN (radio + eink + peripherals)
constexpr int k3v3Pwr = kPinNum(0, 13);         // aux 3V3 rail (REV_2 boards; harmless to assert)
constexpr int kEpdBacklightEn = kEpdBacklight;  // P1.11 = backlight AND panel enable

}

#endif
