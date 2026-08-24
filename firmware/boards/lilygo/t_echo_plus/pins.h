// boards/lilygo/t_echo_plus/pins.h: LilyGO T-Echo Plus (nRF52840) pin map.
#ifndef SKYBLIP_BOARDS_T_ECHO_PLUS_PINS_H
#define SKYBLIP_BOARDS_T_ECHO_PLUS_PINS_H

#include <cstdint>

#if defined(CONFIG_BOARD_T_ECHO_PLUS)
#include <zephyr/devicetree.h>
#endif

#include "hardware/io/io.h"

namespace skyblip::boards::t_echo_plus {

constexpr int kPinNum(int port, int pin) { return port * 32 + pin; }

constexpr int kRadioDio1 = kPinNum(0, 20);
constexpr int kRadioBusy = kPinNum(0, 17);
constexpr int kRadioRst = kPinNum(0, 25);

constexpr int kEpdDc = kPinNum(0, 28);
constexpr int kEpdRst = kPinNum(0, 2);
constexpr int kEpdBusy = kPinNum(0, 3);
constexpr int kEpdBacklight = kPinNum(1, 11);

// NOT a motor drive. This is the DRV2605's enable line: raising it brings the
// waveform driver out of standby and moves nothing on its own, because the pulse
// itself is a mode and a drive value over I2C 0x5A
// (hardware/parts/drv2605/). SoftRF identifies a Plus by that part answering
// (platform/nRF52.cpp:1158-1163) and drives this pin only after configuring it
// (2112-2133). The name is Meshtastic's PIN_DRV_EN.
constexpr int kVibro = kPinNum(0, 8);

// Disputed between our BOM (0x76) and LilyGO's README / SoftRF (0x77): the
// devicetree declares both and the board takes whichever answers.
constexpr uint8_t kBaroAddrPrimary = 0x76;
constexpr uint8_t kBaroAddrAlternate = 0x77;

// What this board's virtual wiring needs to know: which pin belongs to which
// part. The silicon platform ignores it and reads the devicetree instead.
constexpr io::PinRole kPinRoles[] = {
    {io::PinFn::RadioBusy, kRadioBusy},       {io::PinFn::RadioReset, kRadioRst},
    {io::PinFn::RadioIrq, kRadioDio1},        {io::PinFn::EpdDc, kEpdDc},
    {io::PinFn::EpdReset, kEpdRst},           {io::PinFn::EpdBusy, kEpdBusy},
    {io::PinFn::EpdBacklight, kEpdBacklight},
};

constexpr io::PinMap kPinMap{kPinRoles, static_cast<int>(sizeof(kPinRoles) / sizeof(kPinRoles[0]))};

// INFO: fc 24aug26 the checks below prove the two copies agree, never that a pin is right
#if defined(CONFIG_BOARD_T_ECHO_PLUS)
#define SKYBLIP_DEVICETREE_PIN(node_id) \
    (DT_PROP(DT_GPIO_CTLR(node_id, gpios), port) * 32 + DT_GPIO_PIN(node_id, gpios))

static_assert(kRadioBusy == SKYBLIP_DEVICETREE_PIN(DT_NODELABEL(radio_busy_gpio)));
static_assert(kRadioDio1 == SKYBLIP_DEVICETREE_PIN(DT_NODELABEL(radio_dio1_gpio)));
static_assert(kRadioRst == SKYBLIP_DEVICETREE_PIN(DT_ALIAS(radio_reset)));
static_assert(kEpdDc == SKYBLIP_DEVICETREE_PIN(DT_NODELABEL(epd_dc_gpio)));
static_assert(kEpdRst == SKYBLIP_DEVICETREE_PIN(DT_NODELABEL(epd_reset_gpio)));
static_assert(kEpdBusy == SKYBLIP_DEVICETREE_PIN(DT_NODELABEL(epd_busy_gpio)));
static_assert(kEpdBacklight == SKYBLIP_DEVICETREE_PIN(DT_NODELABEL(epd_backlight_gpio)));
static_assert(kVibro == SKYBLIP_DEVICETREE_PIN(DT_ALIAS(vibro)));

#undef SKYBLIP_DEVICETREE_PIN
#endif

}  // namespace skyblip::boards::t_echo_plus

#endif
