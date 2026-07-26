#!/usr/bin/env python3
"""CI flash-size budget gate (3-ARCHITECTURE §1, roadmap E0.3).

Fail if the app .elf text+data exceeds the budget. A comparable e-paper tracker
app is ~668 KB (~81% of the usable region); we set a hard ceiling early because
dieting at 81% full is painful. Default 500 KB (proposed).
Usage: size_check.py <firmware.elf> [budget_bytes]
"""
import subprocess
import sys

DEFAULT_BUDGET = 500 * 1024


def main():
    if len(sys.argv) < 2:
        print("usage: size_check.py <firmware.elf> [budget_bytes]")
        return 2
    import os
    elf = sys.argv[1]
    budget = int(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_BUDGET
    # Prefer the Zephyr SDK toolchain; fall back to the generic ARM/host size.
    # The host `size` may not be able to read an arm-zephyr-eabi ELF at all, so
    # the SDK's own tool is the one that should be found first.
    candidates = [os.environ.get("SIZE"), "arm-zephyr-eabi-size",
                  "arm-none-eabi-size", "size"]
    if not os.path.isfile(elf):
        # Say which file is missing. Falling through to `size` here produced a
        # 12-line CalledProcessError traceback that read like a broken script
        # rather than "the build did not put the .elf where CI looked".
        print(f"ERROR: no such ELF: {elf}")
        return 1
    out = None
    for size_tool in [c for c in candidates if c]:
        try:
            out = subprocess.check_output([size_tool, elf], text=True)
            break
        except FileNotFoundError:
            continue
        except subprocess.CalledProcessError as e:
            print(f"ERROR: {size_tool} failed on {elf}: {e}")
            return 1
    if out is None:
        print("WARN: no *-size tool found; skipping size check")
        return 0
    # text data bss dec hex filename
    line = out.strip().splitlines()[-1].split()
    text, data = int(line[0]), int(line[1])
    used = text + data
    pct = 100.0 * used / budget
    print(f"flash: {used} B used / {budget} B budget ({pct:.1f}%)")
    if used > budget:
        print("FAIL: over flash budget")
        return 1
    print("OK: within flash budget")
    return 0


if __name__ == "__main__":
    sys.exit(main())
