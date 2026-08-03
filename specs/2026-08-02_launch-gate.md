# Launch gate — the second-round check

Read against the tree at `830ebe9` (2026-08-02), and against two shipping implementations we keep as read-only clones: `oss/SoftRF-lyusupov` and `oss/SoftRF-moshe-braner` ("MB"). Their capability claims are read off their own READMEs and source, not measured by us.

This is the **feature and product** half of the check. The bring-up half is `specs/first-flash-readiness.md`: SX1262 commands the chip needs, GNSS configuration, watchdog, low-battery action, LBT threshold, duty-cycle accounting. Nothing here restates it. An item that appears in both is named once, there, and referenced here.

**Position, unchanged: ADS-L on the wire, ALP-TAS on the wire and on the output, simultaneously.** Every gap below is judged against that, not against "be SoftRF". A gap we choose not to close is recorded as deferred so review does not reopen it.

Verdicts: **gate** blocks the first public firmware, **decision** needs a written answer rather than code, **deferred** is a stated no, **ahead** is ours to keep.

---

## What is actually in the tree today

Air interface: ADS-L direct RX/TX and ADS-L uplink RX per SRD-860 Issue 2, plus the **ALP-TAS air-frame codec (FLARM-wire, 2024 protocol)** with `Feature::AlptasRx` enabled and a decrypt-plausibility gate ahead of the traffic table. Output: `$PFLAA`/`$PFLAU`/`$PGRMZ` over BLE. Alarm: three levels off straight-line closure. Power: divider read -> gauge -> status screen, two discharge curves. One product, `skyblip_go`, seven services on T-Echo Plus. Host suite 251 cases; the device image and the twister suite compile in CI only.

That baseline moved twice in the last day. Re-read it before the review meeting rather than trusting this paragraph.

---

## 1. Air interface

| # | Capability | SoftRF | SoftRF MB | skyBlip today | Verdict |
|---|---|---|---|---|---|
| 1.1 | ADS-L SRD-860 | partial | RX, partial relay | full RX/TX, slot and channel map per Issue 2 | ahead — the bet |
| 1.2 | FLARM-generation radio (2024 protocol) | RX+TX | RX+TX, corrected hopping and slots | codec + `AlptasRx`; TX path exists, feature not enabled | **decision** 5.1 — receive is in, transmit is a position, not a task |
| 1.3 | OGNTP | RX+TX | RX+TX | none | deferred |
| 1.4 | FANET / FANET+ | RX+TX | dual-protocol with FLARM, messaging | none | **decision** 5.2 |
| 1.5 | PilotAware P3I | yes | 4-protocol mode | none | deferred — UK-only population |
| 1.6 | Simultaneous multi-protocol | Octave/Duo | up to 4, alternate-protocol periodic TX | two protocols, two bands, one radio | ahead of where we planned to be; follows 1.4 |
| 1.7 | 1090 ES receive | dedicated editions | GNS5892R module on T-Beam | `rx1090` add-on only, running lyusupov's firmware | deferred for Go; dependency in §6 |
| 1.8 | 978 UAT | yes | no | none | deferred — US airspace |
| 1.9 | 2.4 GHz / S-band, Remote ID | LR1110/LR1121 boards | no | none | deferred |
| 1.10 | APRS, MAVLink, ham | yes | no | none | out of scope |
| 1.11 | Relay of shadowed traffic | no | several modes, incl. relay-only | ADS-L `RelayForward` bit exists, unused | deferred, revisit after a season of range data |

## 2. Sensing, alerting, I/O

| # | Item | SoftRF | SoftRF MB | skyBlip today | Verdict |
|---|---|---|---|---|---|
| 2.1 | Battery state | reported | reported | board -> bus -> `PowerService` -> status screen, charge and discharge curves | closed, **except** the value never reaches the companion link — G1 |
| 2.2 | Flight log | Flight Recorder, IGC | compressed logs in flash on both platforms | `FlightLog` named in `project/1-ARCHITECTURE.md` §4.4, absent from the service list | **gate** G2 |
| 2.3 | On-device configuration | limited | full menu on T-Echo | Web Bluetooth only; `ui/input/` is one button header, no settings screen | **gate** G3 |
| 2.4 | Cockpit audibility | single pin | 2-pin differential, external buzzer option | single PWM pin, three levels | **gate** G4 — a measurement, then possibly a BOM change |
| 2.5 | GDL90 out | yes | yes | none | deferred with the Panel SKU (§4) |
| 2.6 | WiFi out (TCP/UDP, client or AP, XCvario) | yes | both at once, IP and port settable | none | deferred with Panel and Helio |
| 2.7 | Second serial port, data bridge | yes | yes | none | deferred with Panel |
| 2.8 | Voice warnings | no | yes, T-Beam DAC | none | deferred |
| 2.9 | Strobe control | SkyStrobe companion | onboard logic | none | deferred — an add-on product, not v1 firmware |
| 2.10 | IMU | some editions | no | BHI260AP on the board, no driver | deferred, stated as unused |
| 2.11 | microSD, RTC, magnetometer | used on some | SD on T-Beam | listed unused in `project/2-DEVICES.md` | deferred, already deliberate |
| 2.12 | Region and frequency plan | global | auto-region by default | `settings.region` exists, 868 EU only | deferred by brand; the market cap is stated now, not discovered later |
| 2.13 | Aircraft-ID filters (ignore, follow) | no | yes | none | deferred |
| 2.14 | Wind estimate, circling collision prediction | no | yes | straight-line closure only | **decision** 5.3 |
| 2.15 | Signed dual-slot OTA | UF2, web flasher | UF2 | MCUboot + MCUmgr/SMP, fail-closed upload gate | ahead |
| 2.16 | PPS-anchored slot timing | polled loop | corrected slots and hopping | hardware capture, host-testable on virtual time | ahead, **unmeasured on silicon** — G6 |
| 2.17 | Scenario replay, simulator, fault seams | no | no | `scenarios/*.json` shared by tests, browser, skyship | ahead, and the reason 1.2 was affordable |

## 3. Non-software

| # | Item | SoftRF / MB | skyBlip today | Verdict |
|---|---|---|---|---|
| 3.1 | CE/FCC marked units | claimed on several OEM-built editions | LilyGO dev boards, no product-level RED/EMC file | **gate** G5. Includes the SRD-860 duty-cycle and LBT evidence whose firmware half is `first-flash-readiness.md` E1/E2 |
| 3.2 | Enclosure, IP rating | IP65/66/67 SKUs, printable cases in-repo | solar-kit enclosure for the ground station only; nothing for Go | **gate** G7 |
| 3.3 | Mounting (yoke, suction, 2.25" panel) | 2.25" instrument form factor and others | none | part of G7 for Go; Panel deferred |
| 3.4 | Antenna and RF front end | documented per edition; SAW+LNA guidance for 1090 | `rx1090` guidance in `project/2-DEVICES.md`; nothing for 868 on Go | **gate** G8 — no public range claim without a measured antenna |
| 3.5 | Aircraft power and installation | ad hoc | MAX3232 and a 14/28 V buck named | deferred with Panel |
| 3.6 | OEM manufacture | LilyGO, Elecrow, Heltec, Seeed, Ebyte, RAK, Olimex | hand-assembled dev boards | deferred until 50+ units, then it is the bottleneck, not firmware |
| 3.7 | Companion display hardware | SkyView EZ / Pico / Pi | tablet over BLE | deferred — the 200x200 panel is the radar |
| 3.8 | Getting a stranger flying | web flasher, per-edition wiki, user guide, mailing list | UF2, the manage page, the site | **gate** G9 |
| 3.9 | Field range evidence | MB collects RX statistics by relative bearing | nothing collected | **decision** 5.4 |

---

## 4. Product surface

`project/2-DEVICES.md` lists five products. `firmware/products/` contains one, `skyblip_go`. Panel, Helio and Bridge are documentation rows: no directory, no board declaration, no service list. The gate is that nothing outside this repo — site, deck, conversation with a club — claims a second product until a directory exists. Dock mode stays unadvertised, as already decided.

---

## 5. Decisions, each answered in writing before the check

An answer of "not this year" is a valid answer. An unanswered row is a blocked review.

### 5.1 ALP-TAS transmit

Receive landed with a codec, a decrypt-plausibility gate and a test file. Transmit is a different question: it puts us on air in a protocol whose ecosystem we did not define, and it is the one item on this page with legal exposure. Required output: a position covering transmit versus receive-only, the IP and trademark reading, who has reviewed it, and the trigger that would change the answer.

### 5.2 FANET

The paraglider and hang population is EU-based, large, and invisible to us. MB runs FANET alongside FLARM or ADS-L on one radio, so slot coexistence has a precedent. Question: v1.1 or never. This is the only protocol on the page I would call undecided rather than deferred.

### 5.3 Alarm model

`core/traffic/alarm.cpp` is straight-line closure on distance and time-to-impact. Gliders circling in a thermal defeat straight-line prediction; MB added circling prediction and a wind estimate for exactly that. `first-flash-readiness.md` F1 already flags the head-on worst case. Question: does v1 ship straight-line with a documented limitation, or does the gate include a circling encounter? Either answer is a committed scenario in `firmware/scenarios/`.

### 5.4 Range and reception telemetry

We have BLE offload and a manage page. Collecting reception statistics from unit one gives us the dataset the incumbents never published, and it is the cheapest item here. Question: opt-in, opt-out or none, and exactly what leaves the device.

---

## 6. External dependencies, stated out loud

- `rx1090` runs **SoftRF Retro Edition MkII** (lyusupov), flashed over SWD. Every 1090 claim we make is a claim about someone else's firmware. Licence, attribution and an upgrade path belong in the Panel/Helio spec before either ships.
- ADS-L 4 SRD-860 Issue 2 is the authority for §1.1; the PDF is `project/reference/2025-09-24_ads-l_4_srd860_issue_2_final.pdf`. Any timing change traces to a clause.

---

## 7. The gate

Green means a runnable check or a named document. Not an opinion in review.

| Gate | Requirement | Evidence that closes it |
|---|---|---|
| **G1** | Battery state reaches the pilot's tablet, not just the panel | state of charge and charging flag in the companion link payload, host test through `core/comms` |
| **G2** | A flight leaves a record | `FlightLog` service in the `skyblip_go` list, records surviving a power cut, offload over BLE, host test |
| **G3** | A pilot with no phone can change region, aircraft type and alarm volume | settings screen over the existing button input, host test through the public entry point |
| **G4** | The alarm is audible in a cockpit | measured dB at 1 m on the bench plus one in-aircraft note; if the single-pin drive fails it, the fix is in the BOM before launch |
| **G5** | Regulatory file open | RED/EMC path chosen, duty-cycle and LBT evidence identified (firmware half: `first-flash-readiness.md` E1/E2), test house contacted. Calendar time: it starts now, it does not finish now |
| **G6** | Slot timing proven on silicon | bench PPS-to-lock histogram from a real T-Echo Plus, twister suite green in CI |
| **G7** | One enclosure, one mount | printable case committed, mount identified, a product photo that is not a taped dev board |
| **G8** | 868 antenna specified | part chosen, gain and pattern stated, one measured range figure traced to `project/research/` before any public range claim |
| **G9** | A stranger can flash and fly | one page: UF2 first flash, BLE config, what each alarm level means, what the device does not see |
| **G10** | `first-flash-readiness.md` P0 and P1 closed | that document's own acceptance tests, green |

Non-gates, recorded so review does not reopen them: the MCU zoo, non-EU regions, UAT978, APRS, ham, MAVLink, Remote ID, SDR receivers, companion displays, strobes, voice, IMU, microSD, RTC, magnetometer, ID filters, relaying.

---

## 8. Order

1. G10 — the bring-up list is the only thing between the host suite and a flown device.
2. G1, G2, G3 — days each, and they are what a pilot notices missing.
3. 5.1 in writing. It is the largest single question on this page and it is not engineering time.
4. G5, G7, G8 started in parallel today: supplier and test-house calendars, not sprints.
5. G6 the moment a board and an SDK are in the same room.
6. G4 and G9 last, because both need a device that flies.
