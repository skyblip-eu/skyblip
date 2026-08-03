# Launch gate — the second-round check

Read against the tree at `830ebe9` (2026-08-02), and against two shipping implementations we keep as read-only clones: `oss/SoftRF-lyusupov` and `oss/SoftRF-moshe-braner` ("MB"). Their capability claims are read off their own READMEs and source, not measured by us.

This is the **feature and product** half of the check. The bring-up half was `specs/first-flash-readiness.md`, closed in pull request 14 and deleted with the work; read it at `git show d190cc6^:specs/first-flash-readiness.md` when a reference below points at it. Nothing here restates it.

## Status, 2026-08-03

The host suite went 407 -> 508 cases closing this list. **G1, G2, G3 and G10 are closed in code. G4, G5, G6, G7, G8 and G9 are closed as far as software and documents reach, and each one now names the single bench or calendar act that finishes it.** Section 5 is answered in writing in `project/3-POSITIONS.md`, which is the authority for those four positions; this file keeps only the pointer.

Six findings came out of closing it that were not on the list and matter more than parts of it:

- **No FLARM-generation device receives ADS-L.** FLARM sells ADS-L as a paid transmit-only extension. Our ADS-L makes us visible to the newest ADS-L instruments and, through the OGN ground network, to SafeSky, SeeYou, SkyDemon and ForeFlight, and to nothing in a glider's own panel. `project/research/does-flarm-receive-adsl.md`, `project/research/who-receives-adsl.md`, position in `project/3-POSITIONS.md` section 0. This is the largest single fact about the product and it was not in this document.
- **The buzzer was never released.** `AlarmService` silenced the annunciator only when the worst level reached zero, and the Zephyr annunciator's tone is continuous, so any traffic inside the 3 km info ring left a tone sounding for the rest of the climb. There was no cadence at all. Fixed in `core/annunciation/`, and it is a G4 item, not a cosmetic one.
- **The carrier-sense ceiling was 0 dBm** where EN 300 220-2 V3.3.1 puts it near -79 dBm at 0 dBd, and the assessment was one instantaneous sample where the standard asks for 160 microseconds. Fixed, derived from the clauses, and the regulatory route we declare is the 1 percent duty cycle the firmware already enforces. `project/research/ce-red-compliance-path.md`.
- **The tone sat below the transducer's resonance**, and the loudest level sat furthest from it. `project/research/alarm-audibility.md`.
- **Nothing respected the negotiated BLE MTU.** The worst-case status frame is 192 bytes and an iOS central commonly settles at an ATT payload of 182, where a notification does not truncate, it fails.
- **A settings write can now land inside a dwell**, because on-device editing is deliberately allowed in flight and the settings blob is on internal flash, whose write stalls the CPU that arms the radio's deadlines.
- **Nothing published a BLE connection event**, so the battery push this list asked for was dead code on silicon, a standing prompt survived the phone that asked for it, and a confirmed upload window stayed open for its full ten minutes after the phone walked away. That last one is a security hole, not a papercut.
- **`$PFLAA`/`$PFLAU`/`$PGRMZ` have no producer.** The formatter is written and tested, the endpoint is routed, the characteristic exists, and no service ever sends a sentence: a pilot who pairs a tablet today sees nothing. The claim at the top of this section 0 was false when this document was written.

- **The O-band dwell could not have heard a skyPost.** Row 1.1 claims ADS-L uplink reception as ours and ahead of both references. The decoder is written and tested and had no product caller; worse, the dwell was armed with the M-band's sync byte, a 64-byte event buffer for a 255-byte codeword, and 100 kbps unshaped where clause C.4 says 200 kbps GMSK in 250 kHz. Three independent reasons it would have received nothing. All three are fixed, the chip model now refuses a burst sent at a rate the modem is not framing, and `scenarios/ground_relay.json` is the fixture.

The last five were found by integration. Each is closed on its own branch, and each is a case the host suite could not fail on before, which is the pattern worth noticing: every one of them lived in the seam between a service and the platform.

**Position, unchanged: ADS-L on the wire, ALP-TAS on the wire and on the output, simultaneously.** Every gap below is judged against that, not against "be SoftRF". A gap we choose not to close is recorded as deferred so review does not reopen it.

Verdicts: **gate** blocks the first public firmware, **decision** needs a written answer rather than code, **deferred** is a stated no, **ahead** is ours to keep.

---

## What is actually in the tree today

Air interface: ADS-L direct RX/TX and ADS-L uplink RX per SRD-860 Issue 2, plus the **ALP-TAS air-frame codec (FLARM-wire, 2024 protocol)** with `Feature::AlptasRx` enabled and a decrypt-plausibility gate ahead of the traffic table. Output: `$PFLAA`/`$PFLAU`/`$PGRMZ` over BLE, which had a formatter and no producer until 2026-08-03. Alarm: three levels off straight-line closure, each with its own annunciation rhythm. Power: divider read -> gauge -> status screen and companion link, two discharge curves. One product, `skyblip_go`, eight services on T-Echo Plus. Host suite 508 cases; the device image and the twister suite compile in CI only.

Two claims this paragraph made on 2026-08-02 were already stale when it was written, and are corrected rather than deleted because they were each load-bearing for a gate: `settings.region` does not exist (it was deleted with its reader in pull request 14, and G3 is closed without it), and `ui/input/` is two headers, a debounced button and the authorising gesture, not one.

---

The rows in sections 1 to 3 are the 2026-08-02 record of the tree and are left as written. Where a row and the gate table in section 7 disagree, the gate table is current.

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

**All four are answered, in `project/3-POSITIONS.md`, which is the authority for them. The summaries below are pointers, not the position.** An answer of "not this year" is a valid answer. An unanswered row is a blocked review.

- **5.1 ALP-TAS transmit: receive only, and the code enforces it.** No licence, no freedom-to-operate opinion, and an exposure that is asymmetric at any volume we can reach. `Feature::AlptasTx` does not exist and the encoder is reachable from tests and the simulator alone. The trigger to revisit, in order of likelihood: FLARM ships ADS-L In and the gap closes without us; a written transmit licence; an authority requirement.
- **5.2 FANET: receive in v1.1, transmit only on evidence.** The protocol is published, receiving is never a licensing question, and the population is drifting to ADS-L on its own. The decision to build it is gated on a measured dwell-loss budget.
- **5.3 Alarm model: straight-line in v1, circling prediction in v1.1.** The limitation is two committed fixtures and three cases: silence through an 11 m crossing in a shared thermal, and a straight-line joiner still caught at 1486 m. MB's algorithm read and priced at 1.5 to 2 weeks.
- **5.4 Telemetry: nothing leaves the device in v1.** Opt-in, off by default, post-flight aggregate in v1.1, with no identity, no track, no exact position and no exact time.

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

| Gate | Requirement | Evidence that closes it | State |
|---|---|---|---|
| **G1** | Battery state reaches the pilot's tablet, not just the panel | state of charge and charging flag in the companion link payload, host test through `core/comms` | **closed.** `status` carries state of charge, validity, the charging flag and the published power level, and pushes unsolicited on a charging flip, a level change or a 5 percent step while a link is up |
| **G2** | A flight leaves a record | `FlightLog` service in the `skyblip_go` list, records surviving a power cut, offload over BLE, host test | **closed.** 24-byte records on the reserved external `log_partition`, 48 flight hours per mebibyte, 62 hours before the ring wraps; a power cut costs the record being programmed and at most 4 seconds of flight; offload and erase over the link, erase behind the physical-presence prompt |
| **G3** | A pilot with no phone can change region, aircraft type and alarm volume | settings screen over the existing button input, host test through the public entry point | **closed**, and region is not in it: the field was deleted with its reader and 868 EU is the only plan (row 2.12). Aircraft type, alarm on/off, volume, units, QNH and the page mask, on the one button, editable in flight on purpose |
| **G4** | The alarm is audible in a cockpit | measured dB at 1 m on the bench plus one in-aircraft note; if the single-pin drive fails it, the fix is in the BOM before launch | **software done, bench owes the number.** Release and cadence fixed, tone moved onto the transducer's likely resonance, target derived (about 93 dB(A) at the ear in a glider, about 100 in a Cessna, ISO 7731), procedure and two BOM fallbacks in `project/research/alarm-audibility.md`. The fitted part could not be pinned from LilyGO's material |
| **G5** | Regulatory file open | RED/EMC path chosen, duty-cycle and LBT evidence identified, test house contacted. Calendar time: it starts now, it does not finish now | **open by one email.** Route chosen (RED module B plus C, the 1 percent duty-cycle channel-access route), standards and their OJEU status listed, technical file as a checklist, calendar backwards from launch, and a ready-to-send enquiry drafted in `project/research/ce-red-compliance-path.md`. The firmware finding it produced is fixed |
| **G6** | Slot timing proven on silicon | bench PPS-to-lock histogram from a real T-Echo Plus, twister suite green in CI | **instrument built, bench owes the run.** Two histograms and three counters in `core/timing/timing_stats.h`, readable over the link with `{"cmd":"timing"}`, thresholds and procedure in `project/research/pps-slot-timing-bench.md`. Twister covers the NVS adapter only: the PPS path needs a `gpio_emul` overlay nobody can verify without the SDK |
| **G7** | One enclosure, one mount | printable case committed, mount identified, a product photo that is not a taped dev board | **decided, blocked on parts and a board in hand.** `project/research/enclosure-and-mount-go.md`: the supplied LilyGO case for a sub-fifty batch, a RAM suction mount, and the thermal and antenna constraints a case must not break. The photo needs a built unit |
| **G8** | 868 antenna specified | part chosen, gain and pattern stated, one measured range figure traced to `project/research/` before any public range claim | **part chosen, range unmeasured.** `project/research/antenna-868-go.md`: ANT-868-CW-QW-SMA, 1.6 dBi, link budget 22 km peak-pattern and 8 to 10 km predicted in the field, with the measurement protocol and the sentence we may publish once it is done |
| **G9** | A stranger can flash and fly | one page: UF2 first flash, BLE config, what each alarm level means, what the device does not see | **closed**, on the website in both languages (`content/pages/first-flight.*`). Every threshold on the page traces to a constant. The walk-up configuration page it would like to link to does not exist yet, and the page says so instead of inventing a URL |
| **G10** | `first-flash-readiness.md` P0 and P1 closed | that document's own acceptance tests, green | **closed** in pull request 14, CI green on all four jobs. What only a bench can settle is listed in that document's own closing section |

Non-gates, recorded so review does not reopen them: the MCU zoo, non-EU regions, UAT978, APRS, ham, MAVLink, Remote ID, SDR receivers, companion displays, strobes, voice, IMU, microSD, RTC, magnetometer, ID filters, relaying.

---

## 8. Order

The original order, kept as the record: G10 first, then G1/G2/G3, then 5.1 in writing, then G5/G7/G8 in parallel, G6 when a board and an SDK are in the same room, G4 and G9 last. That is what was done, in that order, on 2026-08-03.

What is left, in the order it should be picked up:

1. **Send the test-house enquiry** (`project/research/ce-red-compliance-path.md`, the drafted email). Calendar, not engineering, and it is the longest pole on the page.
2. **Order the antenna and the mount** (`antenna-868-go.md`, `enclosure-and-mount-go.md`). Same reason.
3. **Merge the two integration findings**: the BLE payload against the negotiated MTU, and the durable write that must not land inside a dwell. Both are launch blockers in the field and neither is visible from a host suite.
4. **One bench day, in this order**: sleep and receive current (the power spec's A and B), the PPS histogram (`pps-slot-timing-bench.md`), the buzzer sweep and dB at 1 m (`alarm-audibility.md`), sector erase time on the fitted flash part. Every one of them has its acceptance number written down already.
5. **One field day**: two units, the range protocol in `antenna-868-go.md`, and the first public range figure.
6. **`specs/2026-08-03_power-and-field-robustness.md`**, whose P0 items (the rails that are never dropped, both regulators on their LDO) are cheaper than anything left on this page and cost more battery than anything on it.
7. **v1.1, in this order**: the false urgent at 130 m in a gaggle, then circling prediction and the wind estimate, then FANET receive, then the opt-in telemetry aggregate.
