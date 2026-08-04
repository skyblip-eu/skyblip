// hardware/boards/lilygo/t_echo_plus/pins.h: LilyGO T-Echo Plus (nRF52840) pin map.
#ifndef SKYBLIP_HARDWARE_BOARDS_T_ECHO_PLUS_PINS_H
#define SKYBLIP_HARDWARE_BOARDS_T_ECHO_PLUS_PINS_H

#include <cstdint>

#include "hardware/io/io.h"

namespace skyblip::boards::t_echo_plus {

constexpr int kPinNum(int port, int pin) { return port * 32 + pin; }

constexpr int kGnssRx = kPinNum(1, 9);
constexpr int kGnssTx = kPinNum(1, 8);
constexpr int kGnssPps = kPinNum(1, 4);
// L76K power control, and the only way the receiver can be told anything: P1.02
// is its wake line and P1.05 its active-low reset. Two independent sources give
// the same pair: SoftRF platform/iomap/LilyGO_TEcho.h:9-10, which drives both
// high for REV_2 and the Plus (platform/nRF52.cpp:1589-1593), and meshcore
// variants/lilygo_techo/variant.h:139-140, where GPS_EN is 34 (P1.02) and
// PIN_GPS_RESET is 37 (P1.05) with PIN_GPS_RESET_ACTIVE=LOW.
constexpr int kGnssEnable = kPinNum(1, 2);
constexpr int kGnssReset = kPinNum(1, 5);

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

constexpr int kSda = kPinNum(0, 26);
constexpr int kScl = kPinNum(0, 27);

// Disputed between our BOM (0x76) and LilyGO's README / SoftRF (0x77): the
// devicetree declares both and the board takes whichever answers.
constexpr uint8_t kBaroAddrPrimary = 0x76;
constexpr uint8_t kBaroAddrAlternate = 0x77;

constexpr int kButton = kPinNum(1, 10);   // main button (active-low, pull-up)
constexpr int kButton2 = kPinNum(0, 18);  // 2nd button (reset-labelled, usable GPIO)
constexpr int kTouch = kPinNum(0, 11);    // capacitive touch pad

// RGB status LEDs, active-low, and this is the REV_2 / Plus map - NOT the map on
// the older T-Echo, which is the trap here.
//
// SoftRF platform/iomap/LilyGO_TEcho.h:22-24 gives REV_2 GREEN P1.01, RED P1.03,
// BLUE P0.14, and platform/nRF52.cpp:1595-1601 applies exactly those three under
// the one case label that covers NRF52_LILYGO_TECHO_REV_2 and
// NRF52_LILYGO_TECHO_PLUS. nrf52-ogn-tracker variants/t_echo/variant.h:20-24
// independently gives 35u = P1.03, commented "red LED on common T-Echo rev2
// boards", with LED_STATE_ON 0.
//
// meshcore variants/lilygo_techo/variant.h:77-79 disagrees: 13 / 14 / 15. That is
// SoftRF's REV_1 map (iomap:19-21), so that variant targets the older board - and
// P0.13 is this board's aux 3V3 rail (k3v3Pwr below, iomap:82-83), so the two
// cannot both be true of a Plus. Every other pin in this file is already the
// REV_2 map, which is the tie-break.
//
// UNVERIFIED ON HARDWARE, and unevenly sourced: red P1.03 has two independent
// sources, green P1.01 and blue P0.14 rest on SoftRF alone. project/2-DEVICES.md
// has no LED row at all. If a bench says otherwise, the three numbers here and
// the devicetree node are the only places to change - nothing above the board
// knows a pin.
constexpr int kLedGreen = kPinNum(1, 1);
constexpr int kLedRed = kPinNum(1, 3);
constexpr int kLedBlue = kPinNum(0, 14);
// LED_STATE_ON is LOW on every nRF52 T-Echo in both references (SoftRF
// platform/nRF52.h:53, nrf52-ogn-tracker variants/t_echo/variant.h:23), so "off"
// is the pin driven HIGH - which is why the way down releases these lines instead
// of leaving them driven into a rail that has gone (hal/indicator.h park()).
constexpr bool kLedActiveLow = true;

// Buzzer + vibration motor exist ONLY on the T-Echo *Plus*. Pins confirmed from
// the Meshtastic `t-echo-plus` variant: PIN_BUZZER P0.06, PIN_DRV_EN P0.08.
constexpr int kBuzzer = kPinNum(0, 6);  // piezo buzzer, straight off PWM0
// NOT a motor drive. This is the DRV2605's enable line: raising it brings the
// waveform driver out of standby and moves nothing on its own, because the pulse
// itself is a mode and a drive value over I2C 0x5A
// (hardware/parts/drv2605/). SoftRF identifies a Plus by that part answering
// (platform/nRF52.cpp:1158-1163) and drives this pin only after configuring it
// (2112-2133). The name is Meshtastic's PIN_DRV_EN.
constexpr int kVibro = kPinNum(0, 8);

// PIN_POWER_EN gates the radio, the e-paper and the external flash. It is
// asserted in board.c so MCUboot sees the rails up too.
constexpr int kIoPwr = kPinNum(0, 12);          // PIN_POWER_EN (radio + eink + peripherals)
constexpr int k3v3Pwr = kPinNum(0, 13);         // aux 3V3 rail (REV_2 boards; harmless to assert)
constexpr int kEpdBacklightEn = kEpdBacklight;  // P1.11 = backlight AND panel enable

// External flash single-line WP# (IO2) and HOLD# (IO3), parked high by board.c
// and released on the way down. SoftRF iomap/LilyGO_TEcho.h:92-93.
constexpr int kFlashWp = kPinNum(0, 7);
constexpr int kFlashHold = kPinNum(0, 5);

// What this board's virtual wiring needs to know: which pin belongs to which
// part. The silicon platform ignores it and reads the devicetree instead.
constexpr io::PinRole kPinRoles[] = {
    {io::PinFn::RadioBusy, kRadioBusy},       {io::PinFn::RadioReset, kRadioRst},
    {io::PinFn::RadioIrq, kRadioDio1},        {io::PinFn::EpdDc, kEpdDc},
    {io::PinFn::EpdReset, kEpdRst},           {io::PinFn::EpdBusy, kEpdBusy},
    {io::PinFn::EpdBacklight, kEpdBacklight},
};

constexpr io::PinMap kPinMap{kPinRoles, static_cast<int>(sizeof(kPinRoles) / sizeof(kPinRoles[0]))};

}  // namespace skyblip::boards::t_echo_plus

#endif
