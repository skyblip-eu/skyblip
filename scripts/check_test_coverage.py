#!/usr/bin/env python3
"""Fail if a logic module or a part has no host test that reaches it.

Structural gate, not line coverage: every core/ and ui/ module must be included
by some test, and every hardware/parts/<part>/ must carry its own test_<part>.cpp next
to the driver and its model. A part with no test is a datasheet transcription
nothing checks.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "firmware")

REQUIRED_MODULES = [
    "core/util", "core/fec", "core/protocol", "core/gnss", "core/timing",
    "core/traffic", "core/settings", "core/comms", "core/flight", "core/bus",
    "ui/screens", "ui/widgets", "ui/input", "runtime", "simulator/world",
]

TEST_DIRS = ("test", "test/core", "test/ui", "test/products")


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
    for part in parts():
        for f in sorted(os.listdir(os.path.join(FW, "hardware/parts", part))):
            if f.startswith("test_"):
                with open(os.path.join(FW, "hardware/parts", part, f)) as fh:
                    blob.append(fh.read())
    return "\n".join(blob)


def parts():
    d = os.path.join(FW, "hardware/parts")
    return sorted(p for p in os.listdir(d) if os.path.isdir(os.path.join(d, p)))


def main():
    blob = test_blob()
    missing = []
    for mod in REQUIRED_MODULES:
        if (mod + "/") not in blob:
            missing.append(mod)

    for part in parts():
        part_dir = os.path.join(FW, "hardware/parts", part)
        files = os.listdir(part_dir)
        driver = f"{part}.cpp" in files or f"{part}.h" in files
        if driver and f"test_{part}.cpp" not in files:
            missing.append(f"hardware/parts/{part} (no test_{part}.cpp beside the driver)")

    if missing:
        print("FAIL: no host test coverage for:")
        for m in missing:
            print("  -", m)
        return 1
    print("OK: every module and part is reached by a host test.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
