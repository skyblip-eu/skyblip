# Bootloader, install and updates

skyBlip Go keeps the **factory Adafruit nRF52 bootloader** and chain-loads
**MCUboot** from it. Install is a `.uf2` drag-and-drop; updates are signed and
roll back automatically if the new image fails to come up.

```
0x000000  MBR + SoftDevice S140 6.1.1  152 KB   factory, present, NEVER used or shipped
0x026000  MCUboot                       56 KB   ← what the UF2 bootloader starts
0x034000  image-0  (slot0, running)     728 KB   182 sectors
0x0EA000  ─ USER_FLASH_END ─                     nothing writable above here by UF2
0x0EC000  storage (NVS settings)         32 KB
0x0F4000  Adafruit UF2 bootloader        40 KB   factory, recovery path
0x0FE000  MBR params / bootloader settings

external MX25R1635F (2 MB, QSPI)
0x000000  image-1  (slot1, OTA staging) 732 KB   183 sectors = slot0 + 1
0x0B7000  logs                        ~1.29 MB
```

## Why the secondary slot is on the external flash

MCUboot's `swap-using-offset` reserves **one spare sector in the secondary slot**
so sectors have somewhere to move during a swap
(`mcuboot docs/design.md:283-320`). Putting the secondary slot on the external
2 MB part means that sector — and the whole 728 KB mirror — costs no internal
flash. Internal-only A/B would have capped each slot at ~368 KB.

Both slots must report the same sector size for this mode. The nRF52840 page is
4096 B, the MX25R sector is 4096 B, and
`CONFIG_NORDIC_QSPI_NOR_FLASH_LAYOUT_PAGE_SIZE=4096` forces the driver to report
sectors rather than its default 64 KB blocks. **Do not change that value.**

## Why the SoftDevice stays

It is factory-programmed, never linked against and never enabled — Zephyr's own
BLE controller drives the radio. It is not in any artifact we publish. Leaving it
in place buys two things:

- **Reversibility.** SoftRF, Meshtastic and nrf52-ogn-tracker all link at
  0x26000 and call `sd_*`. Erase the SoftDevice and none of them will ever run on
  the device again. Being able to try skyBlip and go back is worth more than
  148 KB we do not need.
- **A recovery path we did not build.** `scripts/mkuf2.py` refuses to produce an
  image that reaches below 0x26000, so we cannot destroy it by accident.

## First install (blank chip, factory or a bricked bootloader)

Only needed if the Adafruit bootloader is missing. **Read the existing
bootloader region off the unit and archive it before doing anything**, because
nobody has yet confirmed the T-Echo *Plus* ships the same bootloader build as the
plain T-Echo:

```sh
nrfjprog --memrd 0x000F4000 --n 0xC000 > techo-plus-bootloader.hex
nrfjprog --memrd 0x10001014 --n 8
```

Then flash the published combined image (third party — we do not redistribute
the SoftDevice):

```sh
nrfjprog --program lilygo_techo_bootloader-0.6.1_s140_6.1.1.hex --chiperase --verify
nrfjprog --reset
```

Confirm `UICR.NRFFW[0] == 0x000F4000` and `UICR.NRFFW[1] == 0x000FE000`, then
double-tap reset: a USB volume named **`TECHOBOOT`** must appear. Nothing else
proceeds until it does.

## Normal install — 60 seconds, no tools

1. Double-tap the reset button. `TECHOBOOT` mounts.
2. Copy `skyblip-go-<version>.uf2` onto it.
3. The device reboots into the new firmware.

## Updates over the air

The device speaks **MCUmgr/SMP** over BLE, so any of these work with the same
`zephyr.signed.bin`:

| Client | Platform |
|---|---|
| the update page on skyblip.eu (Web Bluetooth) | Chrome/Edge on Windows, macOS, Linux, Android |
| nRF Connect Device Manager (Nordic, free) | **iOS**, Android |
| `mcumgr` CLI | development, CI |

Sequence: `img upload` → `img test <hash>` → `os reset`. MCUboot verifies the
ECDSA-P256 signature, swaps, and boots. The application calls
`boot_write_img_confirmed()` only once the radio is up *and* the GNSS parser has
produced a sentence (`App::confirm_image_once_healthy`); if that never happens,
MCUboot restores the previous image on the next boot.

### Authorisation, and why there is no pairing

`CONFIG_BT_SMP` is off, because encrypted GATT characteristics are unreliable
under Web Bluetooth (notably on Windows) and the browser page is the primary
update path. The SMP service is therefore reachable by any peer in range, and
that is safe by design:

- **It cannot install anything.** MCUboot checks the signature.
- **It cannot downgrade you.** `CONFIG_MCUBOOT_DOWNGRADE_PREVENTION`.
- **It cannot waste the secondary slot or reboot you in flight.** The MCUmgr
  upload and reset hooks (`devices/soc/zephyr/mcumgr_hooks.cpp`) defer to
  `core/comms/config.cpp`, which requires the device to be *positively on the
  ground* and an upload window opened by a physical button press. Unknown flight
  state reads as "maybe airborne" and refuses. Takeoff revokes it. It closes on
  disconnect and after 10 minutes.

Flow from the page: send `{"cmd":"dfu"}` → the e-paper asks "Update firmware?" →
the pilot presses the button → upload. `{"cmd":"recovery"}` reboots into
`TECHOBOOT` through the same gate, so recovery never needs the reset button
either.

`0x57` (`DFU_MAGIC_UF2_RESET`) is the only bootloader magic this firmware ever
writes. `0xA8`/`0xB1` would hand control to the bootloader's own *unsigned* BLE
update path — the one SoftRF and Meshtastic use — and appear nowhere in the tree.

## Signing keys

**No signing key is in this repository.** `sysbuild.conf` reads
`SKYBLIP_SIGNING_KEY` from the environment and an unset value fails the build on
purpose: MCUboot's Kconfig default is its own *published* `root-ec-p256.pem`,
which anyone can sign with.

For local development, generate a throwaway key outside the tree:

```sh
python3 -m pip install imgtool
imgtool keygen -k ~/.skyblip/dev-signing.pem -t ecdsa-p256
export SKYBLIP_SIGNING_KEY=~/.skyblip/dev-signing.pem
```

The release key lives offline plus in the `SKYBLIP_SIGNING_KEY_PEM` Actions
secret. Losing it means no device already in the field can be updated over the
air again — they would each need a `.uf2` drag-and-drop. Back it up, and write
the rotation procedure before the first sale.

## The partition map is frozen at first sale

**MCUboot cannot update itself over the air.** Changing any partition boundary
after units ship means every device needs a manual `.uf2` reflash. Before the
first commercial unit, the map above is final.

## Build

```sh
export SKYBLIP_SIGNING_KEY=~/.skyblip/dev-signing.pem
west build -b t_echo_plus firmware --sysbuild
python3 scripts/mkuf2.py firmware/build/merged.hex firmware/build/skyblip-go.uf2
```

`mkuf2.py` is a guard as much as a converter: it refuses to emit a `.uf2` with
any block below `0x26000` (would erase the factory SoftDevice) or at/above
`0x0EA000` (silently dropped by the bootloader, leaving a truncated image).

## Measured, or still to measure

| | |
|---|---|
| slot0 usable | 741 376 B (182 sectors − 1 trailer sector) |
| current image | run `scripts/size_check.py` — CI prints it every build |
| MCUboot size | reserved 56 KB; **to be measured on the first real build** |
| OTA upload time | **to be measured** over Web Bluetooth and over nRF Connect |
| swap + revert time | **to be measured** on hardware, external secondary |
