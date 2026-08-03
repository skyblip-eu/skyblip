# UF2 drag-and-drop on top of MCUboot

Read against the tree at `830ebe9` (2026-08-02). Scope: keep the factory Adafruit
UF2 bootloader as the first flash and the escape hatch, while the field update
path is a signed, dual-slot, auto-reverting MCUboot image delivered over
MCUmgr/SMP. Both, on one device, without either one disabling the other.

Nothing here restates `specs/first-flash-readiness.md` (bring-up: SX1262
commands, GNSS configuration, watchdog) or the feature verdicts in
`specs/2026-08-02_launch-gate.md`. Item 2.15 there says "ahead"; this document is
what that verdict is actually made of, and what is still missing from it.

Legend: **P0** blocks the first public firmware, **P1** is a field-support
problem, **P2** is polish.

---

## 1. The chain

Three programs live in internal flash. The factory bootloader is the reset
vector and stays that way.

| Range | What |
|---|---|
| `0x000000..0x026000` | MBR + SoftDevice S140 6.1.1. Factory, never linked, never enabled: Zephyr drives the radio |
| `0x026000..0x034000` | MCUboot, 56 KB, built by sysbuild |
| `0x034000..0x0EA000` | `slot0`, 728 KB, the signed application |
| `0x0EA000..0x0EC000` | unallocated on purpose, above the UF2 write ceiling |
| `0x0EC000..0x0F4000` | NVS storage, 32 KB |
| `0x0F4000..0x100000` | factory Adafruit bootloader + MBR params + settings, read-only to us |

`slot1` is 0xB6000 on the external 2 MB SPI NOR, exactly the same size as
`slot0`, and the log partition takes the rest. All of this is declared in
`firmware/hardware/boards/lilygo/t_echo_plus/t_echo_plus.dts`.

Boot order: factory bootloader -> MCUboot -> `slot0`. The factory bootloader
treats MCUboot as "the application", so double-tap recovery keeps working no
matter what we put at `0x026000`. That is the whole trick: MCUboot buys signing
and rollback, and costs nothing in recoverability because it is not the reset
vector.

Two constraints follow, and both are already enforced:

- Everything a `.uf2` carries must sit in `0x001000..0x0EA000`
  (`USER_FLASH_START..USER_FLASH_END`, Adafruit_nRF52_Bootloader
  `src/usb/uf2/uf2cfg.h:25-26`). Above that ceiling the bootloader accepts the
  write and silently drops it, leaving a truncated image and a happy progress
  bar.
- Below `0x026000` is the MBR and SoftDevice. Erasing them is not recoverable by
  drag-and-drop and would also end the unit's ability to run SoftRF or
  Meshtastic. Reversibility is a product property, not an accident.

Slot sizing is why the ceiling holds: Zephyr derives imgtool's `--slot-size`
from `slot1`, so `--pad --confirm` pads the image to `slot1`'s length, and that
padded image is what a `.uf2` writes straight into `slot0`. Equal slots put the
trailer at exactly `0x0EA000`. One sector more and the trailer lands above the
ceiling and vanishes.

---

## 2. What already exists

| Piece | Where |
|---|---|
| MCUboot built alongside the app, swap-using-offset, ECDSA-P256, no downgrade | `firmware/products/skyblip_go/sysbuild.conf` |
| Bootloader Kconfig: no console, no serial recovery, external-flash driver, 4 KB layout page, 192 sectors | `firmware/products/skyblip_go/sysbuild/mcuboot.conf` |
| Signed DFU over BLE MCUmgr/SMP, upload and reset hooks | `firmware/products/skyblip_go/prj.conf:126-186` |
| `hal::Dfu` port: `trigger`, `confirm`, `enter_recovery` | `firmware/hal/dfu.h` |
| Zephyr adapter: `boot_request_upgrade`, `boot_write_img_confirmed`, GPREGRET write | `firmware/hardware/platform/zephyr/dfu.h` |
| GPREGRET exposed as a one-byte retention area | board devicetree, `&gpregret1` |
| Policy: refuse in flight, then require a button press | `firmware/core/comms/config.cpp:90-175` |
| Confirm only after radio up and first GNSS fix | `firmware/products/skyblip_go/services/config.cpp:46-51` |
| `.uf2` build: merge the two hex images, guard both ends of the window | `scripts/mkuf2.py` |
| CI: reject MCUboot sample keys, build and publish the `.uf2` | `.github/workflows/ci.yml:140-180` |

Vanilla sysbuild emits no combined hex (that is an nRF Connect SDK feature), so
`mkuf2.py` merges `build/mcuboot/zephyr/zephyr.hex` with
`build/<product>/zephyr/zephyr.signed.confirmed.hex` and calls Zephyr's own
`scripts/build/uf2conv.py` with family `0xADA52840`. No vendored converter.

The confirmed image is not optional in that command. A `slot0` written directly
by a bootloader never goes through a swap, so it never gets the chance to mark
itself good, and an unconfirmed image is reverted on the second boot: the device
would work once, then come back on whatever was there before.

---

## 3. How to enter the bootloader after our firmware is flashed

Three routes, in the order a user should reach for them.

**R1. Double-click RESET, within 0.5 s.** Handled entirely by the factory
bootloader, so it works whatever state our firmware is in, including a
half-written `slot0` or a corrupt MCUboot. The device enumerates as a USB mass
storage volume and a `.uf2` dropped on it is written and booted. Same gesture
the same bootloader uses on this board for SoftRF (`oss/SoftRF-lyusupov`
`software/firmware/binaries/README.md:145`). This is the answer to give a pilot
on the phone.

**R2. From the app, over BLE.** `recovery` on the config channel ->
`ConfigService` refuses it unless the device is on the ground, arms a pending
action and asks for a physical button press -> `hal::Dfu::enter_recovery()`
writes `0x57` (`DFU_MAGIC_UF2_RESET`) into GPREGRET through the retention area
and warm-reboots. The factory bootloader reads GPREGRET on every boot and comes
up as mass storage instead of chain-loading MCUboot. This is for the case where
the device is on a bench, in a case, or its reset button is awkward, and it is
the path the `manage.*` page will drive.

`0xA8` and `0xB1`, the bootloader's own OTA magics, are deliberately written
nowhere in the tree. They hand control to its unsigned BLE update path.

**R3. Nothing on the device is running.** If MCUboot itself is bad, R1 still
works, because the code that implements R1 is at `0x0F4000` and no application
write can reach it. `CONFIG_MCUBOOT_SERIAL=n` and `CONFIG_BOOT_USB_DFU_WAIT=n`
mean MCUboot offers no recovery of its own, on purpose: it has no mass-storage
class, its alternatives need `dfu-util` or `mcumgr` on the host, and R1 needs
neither.

What none of these do: enter the bootloader in flight without hands on the
device. R1 is a physical gesture, R2 is gated on ground state plus a button.
That is the intended asymmetry.

The failure that has no route today is a device whose BLE is broken *and* whose
reset button is unreachable in a sealed enclosure. Noted for the enclosure
decision, not solvable in firmware.

---

## 4. Invariant: the factory bootloader is never erased

It is the only way to install software on this board without a programmer. Lose
it and the unit needs SWD, which means a customer unit is an RMA and a bench
unit is an afternoon. It is also what keeps a buyer free to put SoftRF or
Meshtastic back on hardware they own, which is a position we take deliberately.

So `0x0F4000..0x100000` is not a partition we manage. It is a region we treat as
absent.

What already holds the line:

- `uf2_boot_partition` is declared `read-only` in the board devicetree, and the
  MBR/SoftDevice partition below it likewise, so nothing can be allocated over
  either.
- No writable partition reaches it: `slot0` ends at `0x0EA000`, NVS ends at
  `0x0F4000`, the secondary slot and the logs are on the external part.
- `scripts/mkuf2.py` refuses any merged image that reaches `0x0F4000`, by name,
  in addition to the `USER_FLASH_END` check that makes it unreachable today. The
  named check is the one that survives someone raising the ceiling.
- CI publishes only the `.uf2` and the signed application binaries. No full-chip
  hex leaves the build, so there is no artifact a programmer could use to erase
  the device wholesale.

What can still destroy it, none of it from firmware:

| Action | Why it happens |
|---|---|
| `west flash --erase`, `nrfjprog --eraseall`, J-Link "erase chip" | the reflex when a board misbehaves during bring-up |
| flashing a full-chip hex from another project | wrong artifact, right cable |
| dropping a bootloader `.uf2` (`nosd.uf2` and friends) | deliberate, and the one case with no undo but its own success |

The first two are recoverable with the programmer that caused them: LilyGO's
T-Echo bootloader is published as a hex and reflashing it restores drag-and-drop
for every future install. That is a bench procedure, and it is the reason to
keep one SWD probe and a known-good bootloader hex archived alongside the
release artifacts rather than trusting a download to still exist in three years.

The rule for bring-up: never pass `--erase`. `west flash` erases only the
sectors the image occupies, and the image cannot reach the bootloader.

Accept: the merged-image guard in `mkuf2.py` names `0x0F4000` explicitly and CI
fails if the check is removed (folded into G2's self-check), and a copy of the
factory bootloader hex is archived in the release process.

---

## 5. Gaps

### G1. No image version, so the second OTA will be refused (P0)

`CONFIG_MCUBOOT_DOWNGRADE_PREVENTION=y` requires the incoming version to be
strictly greater than the running one. There is no `VERSION` file in
`products/skyblip_go/` and no `CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION` anywhere, so
every image imgtool signs today is `0.0.0+0`. The first OTA onto a
drag-and-drop-installed unit will work; the next one compares `0.0.0` against
`0.0.0`, finds it not greater, and the bootloader discards the staged image
after the user waited through the upload.

Fix: a Zephyr `VERSION` file in the product directory, `tweak` fed from the CI
run number or the commit count so two builds of the same release are still
ordered.

Accept: CI asserts the version imgtool stamped is not `0.0.0+0`, and asserts it
is greater than the version in the previous published artifact.

### G2. `mkuf2.py` has no test (P0)

The two range assertions are the entire reason the script exists, and nothing
exercises them. They are also the assertions most likely to be quietly defeated
by a partition change, which is exactly the change nobody re-reads this script
for. The boundary case is not hypothetical: the padded confirmed image ends at
precisely `0x0EA000`, one byte from a hard failure.

Fix: a small `pytest`-free self-check next to the script, run in CI: synthetic
hex inputs for below-`0x026000`, above-`0x0EA000`, exactly-`0x0EA000`, and
conflicting-bytes-at-the-same-address.

Accept: the check runs on every push and fails if any guard is removed.

### G3. `zephyr.signed.confirmed.hex` is assumed, not asserted (P1)

CI feeds that filename to `mkuf2.py`. It exists because
`CONFIG_MCUBOOT_GENERATE_CONFIRMED_IMAGE=y` and hex output is on by default. If
either changes, the failure is a missing file at the last step of a long build,
which is survivable, or worse, a fallback to the unconfirmed image, which is
not.

Accept: CI asserts the confirmed hex exists and differs from `zephyr.signed.hex`
before merging.

### G4. Recovery is reachable only over BLE (P1)

`ui/input/` is one button header with no settings screen (launch gate G3), so R2
depends on a working companion page, which does not exist yet, and R1 depends on
the user knowing about the double-click. Nothing on the device says so.

Fix follows the settings screen. Until then the gesture belongs in the printed
quick-start card, not only in a web page.

Accept: the recovery gesture appears in the device's own help screen once there
is one.

### G5. Untested on silicon (P0, blanket)

No swap, no revert, no drag-and-drop install has been observed on a physical
T-Echo Plus. The whole of this document is a design that compiles. The first
bring-up must run, in order: drag-and-drop install, confirm-on-fix, OTA of a
deliberately broken image and observe the automatic revert, OTA of a good image,
then R2 into mass storage and back.

Accept: each of the five steps recorded, with the resulting flash contents, in
the first-flash log.

### G6. Bootloader updates are out of scope, and should stay that way (P2)

The factory bootloader is itself upgradeable by its own `nosd.uf2`. We do not
ship one and should not: it is the only thing on the device that cannot be
recovered from a mistake in the same file that caused it. If a bootloader bug
ever forces this, it is an SWD service operation, not a customer action.

---

## 6. Deliberate positions, so review does not reopen them

- **A drag-and-drop downgrade is always possible.** Downgrade prevention covers
  the OTA path only. Someone holding their own device can put any signed image
  on it, including an old one. Accepted: physical possession.
- **The `.uf2` is not encrypted and not secret.** It is a public artifact. The
  signature is what makes it ours, and the private key is never in the
  repository. CI refuses to publish anything signed with an MCUboot sample key.
- **`CONFIG_BT_SMP` stays off.** The security boundary is the image, not the
  link. Requiring pairing to receive a rescue update buys nothing once every
  image is signature-checked, and costs a support path.
- **We never write the OTA magics.** The factory bootloader's own BLE update
  accepts unsigned images. It is the one capability of that bootloader we
  actively refuse.
