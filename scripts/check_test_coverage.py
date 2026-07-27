#!/usr/bin/env python3
"""Fail if any core/ or ui/ subfolder lacks a mirrored test (roadmap 2.0 gate).

Every logic module must have at least one test_*.cpp that references it. This is
a cheap structural coverage gate (not line coverage) that stops a module from
shipping untested. Run in CI.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "firmware")

# Modules that must be exercised by the host test suite.
REQUIRED = [
    "core/util", "core/fec", "core/protocol", "core/gnss", "core/timing",
    "core/traffic", "core/settings", "core/comms", "core/flight", "ui",
    "devices/drivers",
]


def test_blob():
    blob = []
    for base in ("test/core", "test/ui"):
        d = os.path.join(FW, base)
        if not os.path.isdir(d):
            continue
        for f in os.listdir(d):
            if f.endswith(".cpp"):
                with open(os.path.join(d, f)) as fh:
                    blob.append(fh.read())
    return "\n".join(blob)


def main():
    blob = test_blob()
    missing = []
    for mod in REQUIRED:
        # a module is "covered" if any test includes a header from it
        needle = mod + "/"
        if needle not in blob and (mod + '"') not in blob:
            missing.append(mod)
    if missing:
        print("FAIL: modules with no host test coverage:")
        for m in missing:
            print("  -", m)
        return 1
    print("OK: all required modules have host test coverage.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
