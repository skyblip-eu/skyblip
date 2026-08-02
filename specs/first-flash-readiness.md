# First-flash readiness

What has to be true before this firmware is written to a physical T-Echo Plus and flown. Every item below came out of reading our tree against two shipping implementations on the same silicon:

- `oss/SoftRF-lyusupov` (and the `moshe-braner` fork), nRF52840 + SX1262 + L76K + SSD1681
- `oss/nrf52-ogn-tracker`, Pawel Jalocha's tracker on the same parts

The host suite is green at 251 cases and covers the logic well. Nothing below is a logic bug. The gap is under the test seam: the radio driver is missing commands the chip needs before it does anything, the GNSS receiver is never configured, and there is no watchdog, no low-battery action and no way to turn the device off. `hardware/parts/sx1262/model.h` is more permissive than the SX1262, which is why 251 green cases said nothing about most of this.

Each action states the acceptance test that proves it. An action without a runnable check is not done.

Legend: **P0** blocks the first flash, **P1** produces wrong data or a dead battery, **P2** is what a pilot notices in flight.

---

## A. SX1262 bring-up (P0)

`hardware/parts/sx1262/sx1262.cpp::configure_mband()` sends SetPacketType, SetRfFrequency, the sync-word register write and SetPacketParams, and nothing else.

### A1. SetModulationParams (0x8B)

`MbandConfig::bitrate` and `fdev_hz` (`sx1262.h:29-30`) are never written to the chip. The modem stays at its reset defaults, so it frames nothing at 100 kbps. Reference: OGN `ogn-radio.cpp:1215` sets 100 kbps, 50 kHz deviation, 234.3 kHz RX bandwidth; SoftRF via `radio-sx126x.c:700-800`.

Accept: the model refuses to frame a burst until modulation params are programmed, and a driver test asserts bitrate, deviation, bandwidth and pulse shape reach the chip in the datasheet's order.

### A2. SetDioIrqParams (0x08)

The IRQ status register is gated by IrqMask, which is 0x0000 after reset. `poll()` reads `flags == 0` forever: no RxDone, no TxDone, every transmission reports `Missed`. Both references program it unconditionally.

Accept: a driver test asserts the mask is programmed before RX or TX is entered, and the model raises no IRQ for a bit that was not unmasked.

### A3. SetPaConfig (0x95) and SetTxParams (0x8E)

Output power is set nowhere in the tree. SetPaConfig is also what raises OCP to 140 mA on the SX1262. EU 868.0-868.6 is 25 mW e.r.p., so the power has to be an explicit, bounded number rather than a chip default. References: OGN `ogn-radio.cpp:200-211`, SoftRF `radio-sx126x.c:598-631`, both clamping (SoftRF to 17 dBm, the fork to 19 dBm, over the antenna-mismatch warning LilyGO ships with the board).

Accept: transmit is refused until power is programmed; a test pins the PA config bytes, the ramp time and the regulatory ceiling.

### A4. CalibrateImage (0x98) for 863-870 MHz

Never issued, so the image rejection was never calibrated for our band. OGN forces it at `ogn-radio.cpp:1221`, SoftRF at `radio-sx126x.c:383-410`.

Accept: image calibration is issued for the band that covers the configured frequency, once, and a test proves it happens after the TCXO is up.

### A5. Configuration is issued while the chip is in RX

`hardware/platform/zephyr/rf.h::start()` calls `configure_mband()` on a radio the previous dwell left in continuous RX. SetRfFrequency and SetPacketType are standby-only commands. Both references bracket every slot with standby (OGN `ogn-radio.cpp:826,859`). If this bites on silicon, the second M-band channel is never tuned and it reads as a slot-map regression.

Accept: the model rejects a config command outside standby, which fails today's tests until the driver brackets them.

### A6. Reset pulse has no width

`begin()` does `gpio_.set(reset_, false); gpio_.set(reset_, true);` back to back. NRESET needs to be held low for at least 100 us (SoftRF `almic.cpp:143-205`).

Accept: the pulse width comes from a named constant and the model faults a reset shorter than the datasheet minimum.

### A7. No TX timeout, no TX watchdog

`transmit()` passes `{0,0,0}` to SetTx (no timeout) and nothing recovers a transmission that never completes. With A2 unfixed this is exactly the case that leaves the PA keyed. SoftRF forces a radio reset at air time + 25 ms (`almic.cpp:494-528`).

Accept: SetTx carries a timeout derived from the frame's air time, and a fault-injection test proves a TX that never completes is recovered and counted.

### A8. Radio presence is asserted, never probed

`hardware/boards/lilygo/t_echo_plus/board.h:31` ORs `Capability::Rf` in unconditionally, and `begin()` only proves BUSY goes low, which a dead MISO also does.

Accept: `begin()` reads a register back (sync word at 0x0740, or GetDeviceErrors) and a missing radio is an absent capability rather than a bad assumption.

---

## B. Own-ship data (P1)

### B1. Geoid separation

`core/gnss/nmea.cpp::apply_gga` stores GGA field 9, altitude above **mean sea level**, into `fix_.alt_m`. `core/protocol/adsl.h:144` declares that field **HAE, WGS-84 ellipsoid, G.1.7**. Field 11, the separation, is parsed nowhere. Across central Europe that is 45 to 48 m of systematic under-report against every correctly implemented neighbour, dropped into the middle of the vertical alarm window. OGN: `gps.cpp:526-529` plus `ogn.h:1577`, with a 40.0 m fallback when the receiver omits the field. SoftRF: `Legacy.cpp:266`. Both carry a workaround for the receivers that always report `0.0,M`.

This is the only item on the list that makes us transmit confidently wrong traffic rather than none.

Accept: HAE and MSL are separate values with separate validity; a test pins a real GGA against both; a receiver that omits the field or reports zero falls back to a named constant and says which one it used.

### B2. Integrity and accuracy are all zero

`core/protocol/adsl.cpp::from_own` never touches SIL, SDA, NIC, NACp, GVA or NACv, and the parser drops HDOP (GGA field 8). All-zero reads as "no integrity claimed". OGN derives accuracy from DOP at `ogn.h:1526-1538`.

Accept: DOP reaches own-ship state and maps to the ADS-L integrity fields, with a test over the mapping's boundaries.

### B3. Slot phase is quantised to the service tick

`platform/zephyr/pps.h` latches the edge in the ISR, but `board.h:99` snapshots `ms_since_pps` at the 10 ms service tick and `services/radio.cpp:23` derives the phase from that snapshot: up to 10 ms of quantisation on top of a 5 ms guard.

Accept: the transmit phase is computed from the latched edge at the point of use, and the host executor test pins the residual error below the guard.

### B4. Dead settings

`stealth` is `(void)stealth` at `adsl.cpp:299`. `region`, `power_save`, `rotation` and `callsign` have no non-test reader. A config page that accepts settings the firmware ignores is worse than one that does not offer them.

Accept: every field in `settings::Settings` either has a reader with a test, or is removed from the struct and the JSON schema.

---

## C. GNSS configuration (P1)

`hardware/parts/l76k/l76k.cpp` only drains the UART; the devicetree pins 9600 baud (`t_echo_plus.dts:278`). Nothing is ever sent to the receiver. SoftRF sends three commands to this exact part, 250 ms apart (`GNSS.cpp:1029-1057`).

### C1. Configure the receiver at bring-up

`$PCAS04,7` constellations, `$PCAS03,...` sentence set, `$PCAS11,6` **aviation dynamic model**. Without the last one the module applies pedestrian smoothing and lags in turns.

Accept: the driver owns a configuration sequence, the model asserts the receiver got it, and a receiver that never acknowledges is a degraded capability rather than a silent default.

### C2. Fix rate against the 500 ms staleness rule

At the factory 1 Hz our own G.1.16 rule (`test/core/test_timing.cpp`) suppresses roughly half the transmissions. On the bench that reads as an intermittent transmitter, not as a configuration bug.

Accept: the rate and the baud that carries it are set together, and a scenario pins the transmit cadence at the configured rate.

### C3. Burst-to-PPS latency

Neither reference trusts the NMEA burst as a time source without correcting for how late the sentence is: SoftRF carries a per-chip constant (L76K = 135 ms for RMC, `RF.cpp:236-260`), OGN a `PPSdelay` parameter defaulting to 100 ms.

Accept: the correction is a named part constant, applied where the fix is timestamped, with a test.

---

## D. Housekeeping the device does not have (P0/P1)

### D1. Watchdog (P0)

`runtime/tasks.h:17` declares `kTaskWatchdogMs = 5000` and nothing uses it; `prj.conf` has no `CONFIG_WDT`. SoftRF runs a 12 s hardware watchdog started last and fed from the loop, and uses a deliberate bite as its reboot primitive (`nRF52.cpp:4558-4572, 3240-3292`, including the note that on nRF52 the watchdog cannot be stopped once started).

Accept: the watchdog is armed after setup, fed by the service loop, and a stalled service is provable in a host test of the loop's feed decision.

### D2. Boot self-test on the panel (P0)

`main.cpp:29` returns -1 and the device goes dark when a required capability is missing. SoftRF prints and paints a per-part PASS/FAIL page and keeps running (`nRF52.cpp:2222-2285, 2544-2612`). For a first flash, a panel that names the part that failed is worth more than anything else on this list.

Accept: a boot page renders the capability inventory and the reset reason, degraded parts included, and a host test asserts a missing required capability reaches the panel before the loop refuses to fly.

### D3. Low-battery action (P1)

`services/power.cpp` publishes a percentage and nothing acts on it. SoftRF: 3.5 V low, 3.2 V cutoff, more than two consecutive samples, and a 1.8 V sanity floor so a floating ADC cannot shut the device down (`Battery.cpp:60-77`). Our read-side plausibility window (`platform/zephyr/battery.h:44`) is the right half; the acting half is missing.

Accept: a cutoff decision in `core/power` with the consecutive-sample rule and the sanity floor, tested at both boundaries, and a warning state before it.

### D4. Power off and radio sleep (P1)

No long press, no SYSTEM OFF, no radio sleep. The receiver runs through roughly 980 ms of every second by design, so a T-Echo in a bag is flat inside a day. SoftRF's sequence is `nRF52.cpp:2790-2921`; note `3182-3204`, it spins until the button is released before arming the wake pin or the device wakes immediately.

Accept: a shutdown path that puts the radio in sleep, parks the panel, drops the rails and arms the wake pin, with the button-release guard, driven from a host-tested state machine.

### D5. Reset reason (P1)

Never read. SoftRF maps `POWER->RESETREAS` to seven causes (`nRF52.cpp:781-825`), which is how you tell a watchdog bite from a brown-out in the field.

Accept: the reason is read once at boot, logged, shown on the boot page, and kept for the companion link.

---

## E. Air discipline (P1)

### E1. LBT threshold is a fixed constant

`timing::Transmitter::kBusyThresholdDbm`. OGN tracks a noise-floor EWMA seeded at -105 dBm and raises the threshold 3 dB per retry (`ogn-radio.cpp:77-79, 839-851`). At a noisy site a fixed threshold means we never transmit and nothing says so.

Accept: the threshold is derived from a measured noise floor, the escalation is tested, and a dwell that gave up is counted and visible.

### E2. No duty-cycle accounting

Our LBT plus backoff is the adaptive-frequency-agility route out of the 1% limit in EN 300 220, so this is defensible, but we have no counter to prove it. OGN keeps an explicit 60 s air-time credit (`ogn-radio.cpp:74, 1555`).

Accept: air time per rolling window is counted in `core/timing`, exported, and tested against the 1% figure.

---

## F. What a pilot notices (P2)

### F1. The alarm is worst-case head-on

`core/traffic/alarm.cpp:28-30` sets `closing = own_speed + target_speed` regardless of geometry. Four gliders at 30 m/s within 500 m in one thermal is a permanent level 3. This is the behaviour FLARM is bought for and ours is a range gate. SoftRF at least gates to one notification per 2 s per address (`TrafficHelper.cpp:236-260`); the moshe-braner fork grades the tone by level.

Accept: closing speed comes from the relative velocity vector, a per-target re-notification gate exists, co-circling traffic is suppressed, and each of those is a committed scenario.

### F2. Takeoff and landing is a speed threshold

`services/ownship.cpp:10-15`. OGN uses speed plus four times the climb rate, DOP-derated, 5 s to take off and 10 s to land (`flight.h:66-120`). Flight state gates our DFU lockout and our transmit rate, so a state that flips on a windy ridge start matters twice.

Accept: a debounced state machine in `core/flight` with the climb term, tested at both transitions and against a ridge-start fixture.

### F3. No extrapolation to the transmit instant

OGN predicts the position forward to the exact slot second and reports the residual as a quality metric (`proc.cpp:1160-1163`, `ogn.h:1735-1790`). At 1 Hz we can transmit a position a second old: 50 m at 50 m/s. Compounds with C2.

Accept: the transmitted position is extrapolated to the burst instant under constant speed, turn rate and climb, and the residual is pinned by a test.

### F4. Address hygiene

`device_addr` comes straight from hwinfo. SoftRF remaps the 0xD0, 0xDD, 0xDE, 0xDF prefixes out of the congested FLARM range and 0x11, 0x5B away from Skytraxx and an OGN 0.2.8 decode bug (`SoC.cpp:83-110`).

Accept: the mapping is a pure function in `core/`, tested over the ranges it dodges.

### F5. No first-fix confirmation

SoftRF plays a jingle on first fix; the fork waits 20 s after it before transmitting at all.

Accept: first fix is annunciated once, and the pre-transmit settling period is a named constant.

---

## Bench order

1. D2 and D1: the panel names the failed part, the watchdog is armed. Everything else is easier to debug behind those two.
2. A1 to A8, and teach `models::Sx1262` to reject what the chip rejects, so the host suite starts failing where it should.
3. B1: the one bug that makes us wrong rather than quiet.
4. C1 to C3, then re-check the staleness rule against the real fix cadence.
5. D3 and D4.
6. RF on the bench, antenna fitted, explicit power, LBT threshold measured against the actual noise floor rather than assumed.

No Zephyr SDK on this machine, so the image and the twister suite compile first in CI. Expect to iterate through the pull request.
