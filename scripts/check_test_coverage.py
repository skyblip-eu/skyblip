#!/usr/bin/env python3
"""Fail if a logic module or a part has no host test that reaches it.

Structural gate, not line coverage. Tests live in firmware/test/, one folder per
layer, and never beside the code they test: every core/ and ui/ module must be
included by some test there, and every part with a driver must have
test/hardware/test_<part>.cpp that includes both the driver and its model. A part
with no test is a datasheet transcription nothing checks.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "firmware")

REQUIRED_MODULES = [
    "core/util", "core/fec", "core/protocol", "core/gnss", "core/timing",
    "core/traffic", "core/settings", "core/comms", "core/flight", "core/bus",
    "core/power", "core/annunciation",
    "ui/screens", "ui/widgets", "ui/input", "runtime", "simulator",
]

TEST_DIRS = ("test", "test/core", "test/ui", "test/products", "test/hardware")

PART_TEST_DIR = "test/hardware"


def test_blob():
    blob = []
    for base in TEST_DIRS:
        d = os.path.join(FW, base)
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if f.endswith(".cpp"):
                with open(os.path.join(d, f)) as fh:
                    blob.append(fh.read())
    return "\n".join(blob)


def parts():
    d = os.path.join(FW, "hardware/parts")
    return sorted(p for p in os.listdir(d) if os.path.isdir(os.path.join(d, p)))


def has_driver(part):
    files = os.listdir(os.path.join(FW, "hardware/parts", part))
    return f"{part}.cpp" in files or f"{part}.h" in files


def strays():
    """Test files that crept back in beside the code they test."""
    found = []
    for layer in ("core", "ui", "hal", "hardware", "runtime", "products", "simulator"):
        for dirpath, _, files in os.walk(os.path.join(FW, layer)):
            for f in files:
                if f.startswith("test_") and f.endswith(".cpp"):
                    found.append(os.path.relpath(os.path.join(dirpath, f), FW))
    return sorted(found)


def main():
    blob = test_blob()
    missing = []
    for mod in REQUIRED_MODULES:
        if (mod + "/") not in blob:
            missing.append(mod)

    for part in parts():
        if not has_driver(part):
            continue
        test = os.path.join(FW, PART_TEST_DIR, f"test_{part}.cpp")
        if not os.path.isfile(test):
            missing.append(f"hardware/parts/{part} (no {PART_TEST_DIR}/test_{part}.cpp)")
            continue
        with open(test) as fh:
            body = fh.read()
        # The point of a part test is the driver running against its own model,
        # so a file that includes neither is not one.
        for header in (f"hardware/parts/{part}/{part}.h", f"hardware/parts/{part}/model.h"):
            if header not in body:
                missing.append(f"{PART_TEST_DIR}/test_{part}.cpp (does not include {header})")

    misplaced = strays()

    if missing or misplaced:
        if missing:
            print("FAIL: no host test coverage for:")
            for m in missing:
                print("  -", m)
        if misplaced:
            print(f"FAIL: tests live in firmware/test/, not beside the code they test:")
            for m in misplaced:
                print("  -", m)
        return 1
    print("OK: every module and part is reached by a host test in firmware/test/.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
