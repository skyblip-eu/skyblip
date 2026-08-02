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

// Buzzer + vibration motor exist ONLY on the T-Echo *Plus*. Pins confirmed from
// the Meshtastic `t-echo-plus` variant: PIN_BUZZER P0.06, PIN_DRV_EN P0.08.
constexpr int kBuzzer = kPinNum(0, 6);  // piezo buzzer
constexpr int kVibro = kPinNum(0, 8);   // DRV haptic / vibration motor enable

// PIN_POWER_EN gates the radio, the e-paper and the external flash. It is
// asserted in board.c so MCUboot sees the rails up too.
constexpr int kIoPwr = kPinNum(0, 12);          // PIN_POWER_EN (radio + eink + peripherals)
constexpr int k3v3Pwr = kPinNum(0, 13);         // aux 3V3 rail (REV_2 boards; harmless to assert)
constexpr int kEpdBacklightEn = kEpdBacklight;  // P1.11 = backlight AND panel enable

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
