# Power and field robustness

What the reference implementations do that we still do not, after the first-flash bring-up work. Read against `oss/SoftRF-lyusupov`, `oss/SoftRF-moshe-braner` ("MB") and `oss/nrf52-ogn-tracker`, all on the same nRF52840 + SX1262 + L76K + SSD1681 hardware.

Three documents, no item in two of them:

- `specs/first-flash-readiness.md` is the bring-up list, closed in pull request 14: the SX1262 commands, GNSS configuration, geoid separation, watchdog, self-test page, low-battery cutoff, shutdown state machine, LBT and duty cycle, the alarm and flight-state models, extrapolation.
- `specs/2026-08-02_launch-gate.md` is the product and feature half: protocols, flight log (G2), on-device settings screen (G3), buzzer audibility (G4), battery state on the tablet (G1), enclosure, antenna, regulatory. Its gates are not restated here.
- **This document is the field layer between them**: the things that are not features and not bring-up, that no host test can fail on, and that decide whether a unit survives a season in a cockpit. Most of them are power.

Legend: **P0** the device does not do what it claims, **P1** costs a pilot a flight or a pack, **P2** worth doing before the second batch.

---

## A. Power off does not power off (P0)

`hardware/boards/lilygo/t_echo_plus/board.c` raises the gated peripheral rail (P0.12) at `SYS_INIT` level, because MCUboot needs the external flash before the application exists. **Nothing ever drops it.** Grep the tree: `kIoPwr` and `k3v3Pwr` appear in `pins.h` and nowhere else.

So the shutdown path added in pull request 14 sleeps the radio, parks the panel, waits for the button and arms the wake pin, and then the SoC enters SYSTEM OFF with the radio, the GNSS receiver, the e-paper, the barometer and the external flash still powered. The L76K alone draws in the tens of milliamps with an active fix. An 850 mAh pack does not survive a night in a flight bag, and the device is by every visible sign switched off.

What SoftRF does in `SoC_fini` (`oss/SoftRF-lyusupov/software/firmware/source/SoftRF/src/platform/nRF52.cpp:2790-2921`), in order: GNSS wake pin low, LEDs off then re-configured as inputs, external flash into Deep Power-Down (`0xB9`), the flash's WP and HOLD and CS lines to inputs, GNSS reset to input, **IO_PWR to input so the rail collapses**, I2C and SPI ended, radio reset to input. MB drives every power-enable pin `OUTPUT LOW` before switching it to input, with the reason written down: "clears the output latch AND actively shuts off the external regulator before we release the pin" (`MB/src/platform/nRF52.cpp:2055-2080`).

Actions:

1. **Drop the rails in `system_off()`**, in the order above, and prove the order: the flash gets its Deep Power-Down command before it loses its supply, or the command is a no-op.
2. **The GNSS needs a power state of its own.** There is no enable or reset pin in `pins.h` at all today, so the receiver cannot be told anything. Add what the board actually wires, and use it: off at shutdown, and available later for a receiver backup mode.
3. **Measure the result.** A number in microamps, on the bench, traced into `project/research/`. This item is not closed by code review, and it is the one measurement that decides whether the product ships with a switch.

Acceptance: the sequence is a host-tested state machine, the pin order is asserted, and a measured sleep current is written down.

---

## B. Both regulators are left on their least efficient setting (P0)

Two independent findings, same cause: a chip default nobody overrode.

**The SX1262 is running on its LDO.** `SetRegulatorMode` (0x96) is never issued: grep `regulator` under `hardware/parts/sx1262/`, there is nothing. The reset default is LDO-only, and the DC-DC converter roughly halves the supply current in receive and transmit. Our dwell map has the receiver armed through about 980 ms of every second, so this is the largest single power term in the whole device. SoftRF issues `SetRegulatorMode(DCDC)` as the first line of its low-level init (`.../libraries/arduino-basicmac/src/lmic/radio-sx126x.c:307-324`). It costs one command in `Sx1262::begin()`.

**The nRF52840 may be running on its LDO too.** No `CONFIG_BOARD_ENABLE_DCDC` in `t_echo_plus_defconfig` or `prj.conf`. On this SoC the internal DC-DC saves a comparable fraction of the MCU's own current, and the T-Echo has the inductors fitted (the LilyGO reference designs and Meshtastic's variant enable it). Confirm against the schematic in `project/reference/`, then enable it, because a Kconfig default that happens to be right is not evidence.

Acceptance: the regulator command is in the driver's init sequence with a model assertion, the Kconfig is set with the schematic cited, and the bench current in receive is recorded before and after.

---

## C. There is no power budget and no runtime figure (P1)

Nobody can currently answer "how long does it run", which is the first question any pilot asks and the second question any reviewer asks.

Related, and deliberate for now: the `power_save` setting was deleted in pull request 14 (B4) precisely because it had no reader, with the note that the only honest reader would be receiver duty cycling and the GNSS fix rate. That reader still does not exist. We listen through 980 ms of every second because the two systems we receive transmit across that whole span, so the saving has to come from somewhere else, and the options need writing down rather than guessing:

- the dwell map is a policy in `core/timing`, so a "long endurance" plan that gives up part of the M-band span is a table, not a code path
- the GNSS at 5 Hz was chosen against the 500 ms staleness rule; on the ground, 1 Hz is enough and the fix rate is already a driver constant
- the panel is e-paper and costs nothing to hold, but a full refresh is not free
- the BLE connection interval during a long upload is already tuned; idle advertising is not

Actions: a written power budget per state (receive, transmit, panel refresh, BLE connected, idle, sleep), measured not estimated; a runtime figure for the fitted cell; then decide whether an endurance mode earns its place, and if it does, bring `power_save` back with a reader.

Acceptance: a table in `project/research/` with measured figures, and a stated runtime in the product copy that traces to it.

---

## D. Waking up is not handled (P1)

The reset cause is read, named and shown on the self-test page (pull request 14, D5). Nothing acts on it, and four behaviours in the references depend on that.

1. **Plugging in a charger boots the device.** On the nRF52 a VBUS event is a wake source, and `RESETREAS` records it. SoftRF and OGN both go straight back to SYSTEM OFF when the wake was VBUS and the button is not held (`oss/nrf52-ogn-tracker/src/main.cpp:519-532`; SoftRF `nRF52.cpp:944-951`). MB goes further and offers a **charge mode**: beep out the battery level, then sleep again (`MB/src/platform/nRF52.cpp:2512-2560`). Without this, charging a unit in a bag turns it on, and it stays on.
2. **The bootloader can be entered by accident.** SoftRF writes `GPREGRET = DFU_MAGIC_SKIP (0x6d)` before sleeping so the factory bootloader does not drop into DFU on the next boot (`nRF52.cpp:3213-3226`, magic at `nRF52.h:128`). We write `0x57` for a deliberate UF2 recovery and nothing otherwise, so the next reset after a shutdown is at the factory bootloader's discretion.
3. **A stale latch keeps the device awake.** MB clears `NRF_GPIO->LATCH` and `NRF_POWER->RESETREAS` before every SYSTEMOFF "to prevent stale DETECT assertion" (`MB/src/platform/nRF52.cpp:421-446`). A latched DETECT is a device that will not sleep, and it looks like a flat battery.
4. **Sleep current depends on how you got there.** MB reboots into a magic value in `GPREGRET2` and enters SYSTEMOFF from a clean power-on peripheral state, explicitly because that is what reproduced the low drain they measured (`MB/src/platform/nRF52.cpp:2300-2332`). Worth knowing about before optimising A; worth trying if A's measurement disappoints.

Acceptance: the wake cause decides the boot path, host-tested through the reset-cause classifier that already exists; the bootloader magic is set on the way down; and the sleep-current measurement from A is taken on both entry paths if the first one is poor.

---

## E. A dying cell can corrupt the settings (P1)

We now write flash at runtime: settings on every accepted change (`services/config.cpp:46`), and the flight log when it exists (launch-gate G2). The nRF52 has a power-failure comparator (`POFCON`) and nothing in the tree configures it: grep `pof`, nothing.

Two failure modes, both cheap to close:

- **A write in flight when the cell collapses.** NVS is designed to survive an interrupted write, but the pack sagging under a 22 dBm burst on top of a 3.3 V cell is exactly when a write lands. The cutoff added in D3 acts on a voltage threshold, and the right rule is stronger: below the warning level, stop writing anything that is not the log record that must survive.
- **Write rate and wear.** Nothing rate-limits settings writes. A companion app that patches a value per keystroke wears the NVS sector, and SoftRF's own storage library warns that a single byte write can block for over a second when its log fills (`.../libraries/arduino-NVM/README.md:39-49`). Ours is NVS rather than that library, but the shape of the problem is the same and there is no test that says a hundred patches cost one erase.

Acceptance: POFCON configured with a warning that reaches `core/power`, a host test that a write is refused below the warning level, and a rate limit on settings persistence with the coalescing proven by a test.

---

## F. The device gives no sign it is alive (P1)

E-paper holds its last image with the rails down, which is why the wordmark is painted before power off. The consequence is that **an off device and an on device look identical**, and there is no other indicator: the T-Echo's LEDs appear nowhere in the tree (the only `pwmleds` node in the devicetree is the buzzer, and `pins.h` names no LED).

SoftRF uses the status LED as the only alive signal it has: solid above the low threshold, blinking at 300 ms below it (`src/driver/LED.cpp:204-219`), plus a boot tone ladder and a six-note jingle on first fix. We landed the first-fix chirp in pull request 14 (F5), so the audible half exists.

Actions: put the board's LEDs in the devicetree and behind a role; decide what each state looks like (alive, charging, low, no fix, alarm) and write it down where support can read it; keep it cheap, an LED at 1% duty is not a power term but an LED left on is.

Acceptance: the state-to-indicator mapping is a table in `core/`, host-tested, and the charging case is in it.

---

## G. Battery state reaches the panel and stops there (P2)

The launch gate covers the companion link half as **G1**. Two things it does not name:

- **`$LK8EX1`.** SoftRF emits it once a second alongside `$PGRMZ` (`src/protocol/data/NMEA.cpp:231-264`): pressure altitude, vertical speed, temperature and **battery voltage**, in the one sentence LK8000, XCSoar and their descendants already parse. We emit `$PFLAA`, `$PFLAU` and `$PGRMZ`. This is the cheapest possible route from our gauge to a pilot's tablet and it is a formatting function.
- **What the gauge does not estimate.** No time remaining, and OGN keeps a 32-deep delay line on the voltage specifically to derive a drift rate (`oss/nrf52-ogn-tracker/src/proc.cpp:326-345`). Ours has two curves and a median-of-three, which is better than either reference at reading *state*, and no opinion at all about *rate*. A "40 minutes" reading is worth more to a pilot than a percentage, and it is the same samples.

Acceptance: `$LK8EX1` in `core/protocol/nmea_out` with a checksum test; a time-remaining estimate in `core/power` with a test over a discharge trace, or a written decision not to guess.

---

## H. The gauge is uncalibrated per unit (P2)

The divider ratio is a devicetree fact and Zephyr's voltage-divider driver owns the conversion, which is the right shape. What is missing is any way to correct one unit: two 1% resistors and the SAADC's own gain error put a few tens of millivolts between the reading and the cell, and tens of millivolts is a chunk of the flat middle of a LiPo curve. SoftRF has no per-unit calibration either and its constants are per board; OGN needed a hand-calibrated 5.79 mV per count on the Wio Tracker because the nominal ratio was wrong (`src/main.cpp:99-104`).

Action: one signed offset in settings, set once on the line against a bench supply, applied in `core/power`. It is four lines and it makes every later battery complaint answerable.

---

## I. GNSS robustness beyond configuration (P1/P2)

Pull request 14 configured the receiver (constellations, sentence set, aviation dynamic model, 5 Hz, PPS latency). The references carry a second layer, all of it earned in the field:

| Missing | Reference | Why it matters |
|---|---|---|
| Baud detection and recovery | MB `src/driver/GNSS.cpp:1700-1739`; OGN autobaud `src/gps.cpp:89-96, 1205-1222` | A unit whose receiver comes up at another rate is silently GNSS-less. We assume 9600 forever |
| A wake byte before probing | SoftRF `GNSS.cpp:1383-1387`, one `0x00` then 500 ms | The L76K wakes on UART activity; a cold receiver can miss the first command |
| Receiver identification | SoftRF `$PCAS06` handshake, `GNSS.cpp:981-1010` | Confirms the part before trusting a `$PCAS` dialect, and logs the firmware version for a support case |
| Date and jump sanity | OGN rejects year 1980 as a known MTK lie (`src/ogn.h:737-738`); MB rejects short GGA and >0.15 deg jumps (`MB/src/driver/GNSS.cpp:2226-2236`) | A bad fix transmitted is worse than no fix. We validate the checksum and the A/V flag and nothing else |
| Fix age as validity | SoftRF requires GGA and RMC both, all ages inside 3500 ms (`GNSS.cpp:1487-1503`) | Ours trusts the last sentence to have said `A` |
| VDOP for vertical accuracy | needs GSA, which we removed from the sentence set | Our integrity fields substitute HDOP for VDOP, conservatively, and the note is in the code |
| Cold start and factory reset | SoftRF only for u-blox | A receiver with a poisoned almanac takes twenty minutes to fix, and a pilot reads that as broken |
| Leap seconds | MB queries and persists the count, reboots once when it changes (`MB/src/driver/GNSS.cpp:1610-1679`) | Our UTC comes from RMC, which is already UTC, so this is a note not a task: state that it does not apply |
| No RTC | PCF8563 is fitted on this board and absent from our devicetree | Time across a power cycle, which is what a flight log wants before its first fix |

Acceptance: each row is a driver-level test against `models::L76k` or a written "does not apply". The RTC row is a devicetree question first.

---

## J. Radio sensitivity, frequency and sanity (P1/P2)

- **RX gain boost.** SoftRF writes the boosted-gain value to `REG_RXGAIN` on every `SetRx`, with the trade in the comment: about 2 mA for about 3 dB of sensitivity (`radio-sx126x.c:365-372`). OGN enables it in all three of its FSK configurations. 3 dB is 40% more range on a collision warner and 2 mA is nothing next to item B. We do neither. This is a decision to write down, not a default to inherit.
- **Frequency trim.** OGN carries `RFchipFreqCorr` in tenths of a ppm (`src/parameters.h:52-54`); SoftRF forces it to zero for the SX1262 because the design has a TCXO. Ours has a TCXO too, so zero is defensible, but there is no way to measure the error on the bench and no field to correct it if a batch of TCXOs disappoints.
- **Transmit loopback.** SoftRF suppresses a transmission whose buffer equals the last frame received and reports `$PSRFE,RF loopback is detected on Tx` (`src/driver/RF.cpp:381-396`). That guard exists because it happened.
- **Range sanity on receive.** OGN drops any decoded packet further than 25 km as a mis-decode (`src/proc.cpp:540, 573, 725`). We accept whatever survives the CRC, and a silent miscorrection at the edge of the link budget puts a ghost on the radar. Our own `test_adsl.cpp` already counts silent miscorrections, so we know the rate is not zero.
- **Chip temperature.** The SX1262 has none worth reading (OGN says so and falls back to the nRF52 die temperature, `src/proc.cpp:1112-1114`). Ours reports neither, and die temperature is one line and useful in a status payload.

Acceptance: gain boost decided in writing and tested if adopted; a range sanity gate with a test; a die-temperature field in the status reply.

---

## K. The self-test does not know what it is running on (P1)

The self-test page from pull request 14 (D2) reports each capability PASS, FAIL or absent. It assumes the board it was compiled for. LilyGO does not oblige:

- **The e-paper panel is not one part.** SoftRF fingerprints it by clocking registers 0x2D and 0x2E over bit-banged SPI inside a critical section, against a table of five panel signatures actually shipped in T-Echos (`nRF52.cpp:3775-3900`), and it has a per-revision quirk it had to discover: "SYX 1942 revision of D67 display can use power_off() after partial update, SYX 1948 revision - can not" (`src/driver/EPD.cpp:861-865`). We drive one init sequence and one refresh policy.
- **The haptic motor may not be a motor on a pin.** `pins.h` names `kVibro` as P0.08 from the Meshtastic variant and calls it "DRV haptic / vibration motor enable", and our annunciator drives it as a plain GPIO with a timer. SoftRF identifies the T-Echo **Plus** by the presence of a **DRV2605** on I2C (`nRF52.cpp:1144-1160`). If the part on our board is that driver, an enable pin alone brings it out of standby and produces no pulse: the DRV2605 needs a mode, a waveform and a `GO` bit over I2C. **Unverified.** It is the first thing to check on the bench, because the alarm's escalation to vibration is currently a claim in the test suite that has never met the hardware.
- **The buzzer, the IMU and the touch pad.** SoftRF probes the buzzer by reading its pin high-Z against a pull-up, and the IMU only after a 90 ms delay ("MPU9250 or ICM20948 start-up time for register R/W is 11-100 ms"). We drive the buzzer blind and use neither the IMU nor the touch pad, which `project/2-DEVICES.md` already records as deliberate.
- **No I2C bus scan.** One loop, and the self-test page stops guessing which of two BME280 addresses answered.

Acceptance: a panel identification step with the observed signatures in a table; the DRV2605 question answered on hardware and the annunciator fixed if it is one; an I2C scan on the self-test page.

---

## L. Diagnostics leave the device only as a page (P2)

USB CDC is configured as a console and there is no shell, no status dump, no counters off the device. Both references treat that as basic equipment: OGN exports per-protocol TX and RX counts, decoded counts, background RSSI, radio live and dead milliseconds and packet rate every ten seconds (`src/ogn-radio.cpp:1559-1573`) plus a `$POGNR` sentence and a `$POGNS` key-value console protocol with about thirty-five keys; its console also offers a status dump, a flash listing, a traffic listing and a reset on control keys (`src/main.cpp:783-846`).

We already compute most of it: `RadioService` holds the noise floor, the LBT threshold, the give-up count and the duty permille; `bus::State` holds rx_ok, rx_bad, tx_ok, tx_busy and the GNSS fix count; the extrapolation residual is measured. None of it is reachable from a laptop.

Action: one status dump over the console and the same payload in the status JSON. Not a shell, not thirty-five keys: one line per subsystem, and the counters that already exist.

Acceptance: a host test over the formatter, and the fields named in `schemas/`.

---

## M. Uptime discipline (P2)

`hal::Clock::millis()` is a 32-bit millisecond counter, so it wraps at 49.7 days, and services compare it with subtraction in a dozen places. Some of the wave-3 and wave-4 work was written wrap-safe on purpose and says so in its tests; the rest is untested against a wrap. SoftRF gave up and reboots the device after 46 days to dodge it entirely (`src/platform/nRF52.h:270-276`), which is a legitimate answer for a ground station and a poor one for an aircraft.

Action: pick one rule (unsigned subtraction everywhere, no absolute comparisons) and add the wrap case to the services that hold deadlines: the watchdog, the shutdown machine, the LBT backoff, the air-time buckets, the traffic age-out. It is a test each, on code that is already written.

---

## Order

1. **A and B.** Together they decide whether the product has a believable battery story. Both are small; the measurement is the work.
2. **D and E.** A device that boots when charged, or corrupts its settings when flat, generates support load out of proportion to the fix.
3. **K's DRV2605 question**, on the bench, before anyone claims the alarm vibrates.
4. **C**, once A and B are measured, because the budget is only worth writing after the two biggest terms are settled.
5. **F, G, I, J**, in whatever order the first units make loudest.
6. **L and M** before the second batch.
