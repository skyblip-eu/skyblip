# skyBlip → Zephyr migration

Status: **Arduino removed; Zephyr is the only build; bring-up pending on real
hardware.** The port was cheap because the firmware is already hexagonal — the
framework was always a swappable adapter, not a dependency of the logic.

What is done: all Arduino/PlatformIO code deleted (`devices/soc/nrf52`, the
Arduino `main.cpp`, `platformio.ini`); the Zephyr shell is now `main.cpp`; the
SSD1681 e-paper driver is implemented and host-tested; the T-Echo power-gating
(IO_PWR / 3V3_PWR) is handled at boot; all radio/EPD/GNSS pins are set from the
published T-Echo pin maps; CI builds the Zephyr image. 102 host tests pass.

## Why

- **C++20 everywhere.** The Zephyr SDK ships modern GCC (12–14) for *every*
  target, including nRF52840 — impossible on PlatformIO's GCC-7/9 Arduino
  toolchain. This unblocks moving shared `core/` to C++20 (e.g. deleting the
  hand-rolled `core/util/span.h` for `std::span`, adding `concepts` to the HAL).
- **One RTOS, both product lines.** Replaces the *dead* `platform-espressif32`
  and the *stale* `nordicnrf52` with a single, vendor-backed, maintained stack.
- **Secure signed DFU.** MCUboot A/B updates with rollback — a real upgrade over
  Adafruit serial DFU for a field-deployed, safety-adjacent device.
- **Nordic-grade BLE** and real power management / threading primitives.

## The architectural change that made it cheap

Everything that is *not* framework-specific was already isolated. The one change
this migration required in shared code was to **extract the composition root out
of the Arduino entry point** into a framework-agnostic object:

```
products/skyblip/app.{h,cpp}  App: wiring + service loop, pure C++, zero
                              framework headers. Host-tested.
products/skyblip/main.cpp     the (Zephyr) shell → constructs adapters, drives App
```

`App` depends only on `core/`, `hal/` ports, and the shared drivers. The shell
does exactly two things: construct the SoC's adapters and call `App::setup()` /
`App::step()`. Because that boundary is clean, a second shell for another
framework/SoC (e.g. ESP-IDF for skyPost) is purely additive — the logic never
moves. This is enforced on the host by
`test/products/test_app.cpp`, which links the **real** `App` against fakes with
no framework present (the §8 acceptance invariant, finally given teeth).

## What is new (Zephyr-only, all `#ifdef __ZEPHYR__`-guarded — inert on host)

```
devices/soc/zephyr/
  zephyr_io.h          io::Gpio/Spi/Uart over Zephyr device API
  zephyr_clock.h       hal::Clock over k_uptime
  zephyr_kvstore.h     hal::KvStore over NVS (storage partition)
  zephyr_annunciator.h hal::Annunciator over PWM buzzer + GPIO vibro
  zephyr_dfu.h         hal::Dfu over MCUboot (boot_request_upgrade + reboot)
  zephyr_ble.h / ble.cpp  hal::Link over BT GATT
products/skyblip/main.cpp  the Zephyr composition root (+ power-gate sequencing)

CMakeLists.txt  prj.conf  sysbuild.conf  west.yml   Zephyr build
boards/lilygo/t_echo_plus/…                          custom board (HWMv2)
```

**Unchanged and reused as-is:** all of `core/`, all of `ui/`, `hal/*` ports,
`devices/io/io.h`, and the **SX1262 + SSD1681 drivers** (they ride on
`io::Spi`/`io::Gpio`, so they port for free). The host test suite
(`make test`) is framework-independent and untouched — it remains the safety net
throughout.

## Build

```
west init -l firmware        # register this manifest
west update                  # fetch Zephyr v4.1 + modules (hal_nordic, mcuboot…)
west build -b t_echo_plus firmware --sysbuild
west flash
```

## Bring-up checklist (the hardware spike)

These need a board + the Zephyr SDK; they are the real validation, in order of
risk:

1. **Pins/devicetree.** `boards/lilygo/t_echo_plus/t_echo_plus.dts` is set from
   the Meshtastic `t-echo-plus` variant (Plus-specific), cross-checked with the
   published T-Echo pin maps. Radio SPI, e-paper, GNSS UART, and the Plus-only
   buzzer (P0.06) / vibro-DRV_EN (P0.08) are all confirmed. No open pin unknowns.
2. **Power rails.** `main.cpp::power_up_rails()` drives PIN_POWER_EN (P0.12) +
   the aux 3V3 rail (P0.13) high with a settle delay before any SPI — confirm
   the radio + EPD power on (if the radio reads back nothing, this is why).
3. **SX1262 TCXO + slot timing.** The driver now issues SetDIO2AsRfSwitchCtrl +
   SetDIO3AsTcxoCtrl @ 1.8 V + Calibrate in `begin()` (required on this board —
   the radio has no clock otherwise). Bring the modem to Rx over `ZephyrSpi`
   (SPIM3); validate PPS-disciplined TDMA slot timing.
4. **E-paper (SSD1681):** driver implemented + host-tested; on `spi2`. Confirm a
   real refresh (full + partial cadence) and BUSY handling on the panel.
5. **BLE.** `ble.cpp` — confirm advertising, subscribe, notify, and inbound
   config frames reaching `App::on_link_rx`.
6. **NVS settings** round-trip via `ZephyrKvStore`.
7. **MCUboot DFU** over BLE SMP: stage → mark → swap → confirm/rollback.

## Run it on your desktop first (no hardware)

`SimHarness` (`products/sim/harness.h`) is a virtual T-Echo running the real
firmware with host adapters. Two frontends drive it: `make sim` (terminal) and
`make web` (browser/WASM). Both let you page-switch, toggle backlight, power the
panel, and inject radio traffic. Because `App` is framework-agnostic, the
simulator, the Zephyr shell, and the host tests all drive the *same* logic — so
most of the bring-up (UI, config state machine, traffic, driver command
sequences) is validated before the board arrives. What the simulator can't
cover is the physical layer: real SX1262 RF/timing, the panel, BLE, and NVS.

## Sequencing (recommended)

1. Keep the Arduino env building until the Zephyr nRF port reaches RF/BLE parity.
2. Do the nRF52840 spike first (Nordic-blessed, lowest risk).
3. Only then evaluate **skyPost/ESP32-S3** on Zephyr. If Espressif's Zephyr
   peripheral/Wi-Fi gaps don't meet skyPost, keep it on ESP-IDF — the
   ports-and-adapters design lets the two product lines run different frameworks
   behind the same `io`/`hal` interfaces.
4. Once both targets build on modern GCC, flip shared code (`core/`, `ui/`) to
   C++20 (`prj.conf` already sets `CONFIG_STD_CPP20`; bump the host `Makefile`
   and `make test` in lockstep).
