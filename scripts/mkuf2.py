#!/usr/bin/env python3
"""Build the drag-and-drop install artifact, and refuse to build a dangerous one.

    mkuf2.py <merged.hex> <out.uf2>

`merged.hex` is what sysbuild produces: MCUboot at boot_partition plus the signed
application at slot0. Converting it to UF2 gives the single file a pilot drops on
the TECHOBOOT volume.

The two range assertions are the point of this script. The factory Adafruit
bootloader will write any address inside
    USER_FLASH_START .. USER_FLASH_END  =  0x001000 .. 0x0EA000
(Adafruit_nRF52_Bootloader src/usb/uf2/uf2cfg.h:25-26, ghostfat.c in_app_space),
and it does so without complaint. So:

  * A block below 0x026000 would land on the factory MBR + SoftDevice. Erasing
    those makes the device unable to run SoftRF/Meshtastic ever again and breaks
    the bootloader's own fallback DFU. Reversibility is a feature; guard it.
  * A block at or above 0x0EA000 is SILENTLY REFUSED by the bootloader - the
    copy appears to succeed and the device is left with a truncated image.

Both are cheap to check here and expensive to discover in the field.
"""
import os
import pathlib
import subprocess
import sys

SOFTDEVICE_END = 0x00026000
USER_FLASH_END = 0x000EA000
UF2_FAMILY_NRF52840 = "0xADA52840"


def hex_ranges(path):
    """Contiguous [start, end) byte ranges covered by an Intel HEX file."""
    covered = set()
    offset = 0
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line.startswith(":"):
            continue
        count = int(line[1:3], 16)
        addr = int(line[3:7], 16)
        rectype = int(line[7:9], 16)
        data = line[9:9 + 2 * count]
        if rectype == 0x04:
            offset = int(data, 16) << 16
        elif rectype == 0x02:
            offset = int(data, 16) << 4
        elif rectype == 0x00:
            covered.update(range(offset + addr, offset + addr + count))
    if not covered:
        return []
    ranges = []
    ordered = sorted(covered)
    start = prev = ordered[0]
    for a in ordered[1:]:
        if a != prev + 1:
            ranges.append((start, prev + 1))
            start = a
        prev = a
    ranges.append((start, prev + 1))
    return ranges


def zephyr_uf2conv():
    """Zephyr already ships the converter; don't vendor a second one.

    ZEPHYR_BASE is exported by `west build` and by the CI setup action. The
    repo-relative fallbacks cover both west workspace layouts: topdir at the repo
    root (`west init -l firmware`) and the repo as a subdirectory of the topdir.
    """
    candidates = []
    if os.environ.get("ZEPHYR_BASE"):
        candidates.append(pathlib.Path(os.environ["ZEPHYR_BASE"]))
    here = pathlib.Path(__file__).resolve()
    candidates += [p / "zephyr" for p in here.parents[1:4]]
    for base in candidates:
        tool = base / "scripts" / "build" / "uf2conv.py"
        if tool.is_file():
            return tool
    sys.exit(
        "ERROR: cannot find Zephyr's scripts/build/uf2conv.py. "
        "Set ZEPHYR_BASE or run from inside the west workspace. Tried: "
        + ", ".join(str(c) for c in candidates)
    )


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    merged = pathlib.Path(sys.argv[1])
    out = pathlib.Path(sys.argv[2])
    if not merged.is_file():
        return fail(f"no such hex: {merged}")

    ranges = hex_ranges(merged)
    if not ranges:
        return fail(f"{merged} contains no data records")

    print(f"{merged.name} covers:")
    for start, end in ranges:
        print(f"  0x{start:08X} - 0x{end - 1:08X}  ({end - start} bytes)")

    low = min(start for start, _ in ranges)
    high = max(end for _, end in ranges)
    if low < SOFTDEVICE_END:
        return fail(
            f"image starts at 0x{low:06X}, below 0x{SOFTDEVICE_END:06X}. "
            "This would overwrite the factory MBR/SoftDevice. Check that "
            "CONFIG_USE_DT_CODE_PARTITION=y and that boot_partition is at 0x26000."
        )
    if high > USER_FLASH_END:
        return fail(
            f"image ends at 0x{high:06X}, past USER_FLASH_END 0x{USER_FLASH_END:06X}. "
            "The UF2 bootloader silently drops writes above that, so the device "
            "would be flashed with a truncated image."
        )

    subprocess.check_call([
        sys.executable, str(zephyr_uf2conv()),
        str(merged), "-c", "-f", UF2_FAMILY_NRF52840, "-o", str(out),
    ])
    print(f"OK: {out} ({out.stat().st_size} bytes), family {UF2_FAMILY_NRF52840}")
    return 0


def fail(msg):
    print(f"FAIL: {msg}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
