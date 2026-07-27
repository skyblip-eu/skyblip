#!/usr/bin/env python3
"""Build the drag-and-drop install artifact, and refuse to build a dangerous one.

    mkuf2.py <out.uf2> <in1.hex> [in2.hex ...]

The inputs are the two images sysbuild builds separately - MCUboot at
boot_partition and the signed, confirmed application at slot0 - which this script
merges. Vanilla Zephyr sysbuild does not emit a combined `merged.hex` (that is an
nRF Connect SDK feature), and merging Intel HEX is a dozen lines on top of the
parser this script already needs for its range checks, so it does it here rather
than taking a dependency on `mergehex`.

The application image must be the *confirmed* one. A slot0 written directly by a
bootloader never goes through a swap, so it never gets a chance to mark itself
good, and an unconfirmed image would be reverted on the second boot.

The result is the single file a pilot drops on the TECHOBOOT volume.

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


def read_hex(path, into):
    """Merge an Intel HEX file into an {address: byte} map."""
    offset = 0
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line.startswith(":"):
            continue
        count = int(line[1:3], 16)
        addr = int(line[3:7], 16)
        rectype = int(line[7:9], 16)
        payload = bytes.fromhex(line[9:9 + 2 * count])
        if rectype == 0x04:
            offset = int(line[9:9 + 2 * count], 16) << 16
        elif rectype == 0x02:
            offset = int(line[9:9 + 2 * count], 16) << 4
        elif rectype == 0x00:
            for i, byte in enumerate(payload):
                a = offset + addr + i
                if a in into and into[a] != byte:
                    sys.exit(f"ERROR: images disagree at 0x{a:08X} ({path})")
                into[a] = byte


def ranges_of(byte_map):
    """Contiguous [start, end) ranges covered by an {address: byte} map."""
    if not byte_map:
        return []
    ranges = []
    ordered = sorted(byte_map)
    start = prev = ordered[0]
    for a in ordered[1:]:
        if a != prev + 1:
            ranges.append((start, prev + 1))
            start = a
        prev = a
    ranges.append((start, prev + 1))
    return ranges


def write_hex(byte_map, path):
    lines = []
    upper = None
    for start, end in ranges_of(byte_map):
        for chunk in range(start, end, 16):
            data = bytes(byte_map[a] for a in range(chunk, min(chunk + 16, end)))
            if (chunk >> 16) != upper:
                upper = chunk >> 16
                rec = bytes([2, 0, 0, 4, (upper >> 8) & 0xFF, upper & 0xFF])
                lines.append(":%s%02X" % (rec.hex().upper(), (-sum(rec)) & 0xFF))
            rec = bytes([len(data), (chunk >> 8) & 0xFF, chunk & 0xFF, 0]) + data
            lines.append(":%s%02X" % (rec.hex().upper(), (-sum(rec)) & 0xFF))
    lines.append(":00000001FF")
    path.write_text("\n".join(lines) + "\n")


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
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    out = pathlib.Path(sys.argv[1])
    inputs = [pathlib.Path(p) for p in sys.argv[2:]]

    byte_map = {}
    for path in inputs:
        if not path.is_file():
            return fail(f"no such hex: {path}")
        read_hex(path, byte_map)
        print(f"merged {path}")

    ranges = ranges_of(byte_map)
    if not ranges:
        return fail("inputs contain no data records")

    print("combined image covers:")
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

    merged = out.with_suffix(".merged.hex")
    write_hex(byte_map, merged)
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
