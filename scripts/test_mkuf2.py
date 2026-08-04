#!/usr/bin/env python3
"""Self-check for mkuf2.py: the range guards and the Intel HEX round trip.

    python3 scripts/test_mkuf2.py

Runs on stdlib alone and never invokes Zephyr's uf2conv.py, so it is a host job
in CI rather than a step of the 5-minute device build. What it protects is the
window 0x026000..0x0EA000: below it are the factory MBR and SoftDevice, above it
the bootloader accepts writes and drops them, and at 0x0F4000 sits the factory
UF2 bootloader, which is the only way to install anything on this board without
a programmer. A partition edit is what quietly defeats those checks, and a
partition edit is exactly the change nobody re-reads mkuf2.py for.
"""
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import mkuf2  # noqa: E402


def span(start, length, fill=0xA5):
    return {start + i: fill for i in range(length)}


def hex_file(byte_map, directory, name="in.hex"):
    path = pathlib.Path(directory) / name
    mkuf2.write_hex(byte_map, path)
    return path


class RangeGuards(unittest.TestCase):
    def test_the_window_constants_are_the_partition_map(self):
        # t_echo_plus.dts: boot_partition 0x26000, slot0 ends 0xEA000,
        # uf2_boot_partition 0xF4000. Changing one here without changing the
        # devicetree is the failure this file exists to catch.
        self.assertEqual(mkuf2.SOFTDEVICE_END, 0x00026000)
        self.assertEqual(mkuf2.USER_FLASH_END, 0x000EA000)
        self.assertEqual(mkuf2.UF2_BOOTLOADER_START, 0x000F4000)

    def test_a_block_below_the_softdevice_end_is_refused(self):
        problem = mkuf2.check_window([(0x00025000, 0x00026000)])
        self.assertIsNotNone(problem)
        self.assertIn("MBR/SoftDevice", problem)

    def test_a_block_one_byte_below_the_softdevice_end_is_refused(self):
        problem = mkuf2.check_window([(0x00025FFF, 0x00030000)])
        self.assertIsNotNone(problem)
        self.assertIn("MBR/SoftDevice", problem)

    def test_a_block_above_user_flash_end_is_refused(self):
        problem = mkuf2.check_window([(0x00026000, 0x000EA001)])
        self.assertIsNotNone(problem)
        self.assertIn("USER_FLASH_END", problem)

    def test_a_block_in_the_nvs_area_is_refused_for_the_write_ceiling(self):
        problem = mkuf2.check_window([(0x00026000, 0x000F4000)])
        self.assertIsNotNone(problem)
        self.assertIn("USER_FLASH_END", problem)

    def test_a_block_reaching_the_factory_bootloader_names_it(self):
        problem = mkuf2.check_window([(0x00026000, 0x000F4001)])
        self.assertIsNotNone(problem)
        self.assertIn("0x0F4000", problem)
        self.assertIn("bootloader", problem)

    def test_an_image_ending_exactly_at_user_flash_end_is_accepted(self):
        # The real case: imgtool pads the confirmed image to slot1's length, so
        # the trailer lands on the last byte before the ceiling.
        self.assertIsNone(mkuf2.check_window([(0x00026000, 0x000EA000)]))

    def test_the_two_sysbuild_images_together_are_accepted(self):
        self.assertIsNone(mkuf2.check_window([
            (0x00026000, 0x00034000),
            (0x00034000, 0x000EA000),
        ]))


class Merging(unittest.TestCase):
    def test_two_images_merge_into_one_byte_map(self):
        with tempfile.TemporaryDirectory() as d:
            a = hex_file(span(0x00026000, 32, 0x11), d, "a.hex")
            b = hex_file(span(0x00034000, 32, 0x22), d, "b.hex")
            merged = {}
            mkuf2.read_hex(a, merged)
            mkuf2.read_hex(b, merged)
            self.assertEqual(len(merged), 64)
            self.assertEqual(merged[0x00026000], 0x11)
            self.assertEqual(merged[0x00034010], 0x22)

    def test_conflicting_bytes_at_the_same_address_fail(self):
        with tempfile.TemporaryDirectory() as d:
            a = hex_file(span(0x00026000, 16, 0x11), d, "a.hex")
            b = hex_file(span(0x00026000, 16, 0x22), d, "b.hex")
            merged = {}
            mkuf2.read_hex(a, merged)
            with self.assertRaises(SystemExit):
                mkuf2.read_hex(b, merged)

    def test_identical_bytes_at_the_same_address_are_not_a_conflict(self):
        with tempfile.TemporaryDirectory() as d:
            a = hex_file(span(0x00026000, 16, 0x11), d, "a.hex")
            merged = {}
            mkuf2.read_hex(a, merged)
            mkuf2.read_hex(a, merged)
            self.assertEqual(len(merged), 16)

    def test_ranges_of_splits_on_a_gap_and_keeps_a_run_whole(self):
        byte_map = span(0x1000, 16)
        byte_map.update(span(0x2000, 16))
        self.assertEqual(mkuf2.ranges_of(byte_map),
                         [(0x1000, 0x1010), (0x2000, 0x2010)])
        self.assertEqual(mkuf2.ranges_of({}), [])


class IntelHex(unittest.TestCase):
    def test_a_map_round_trips_through_write_hex_and_read_hex(self):
        original = span(0x00026000, 300, 0x5A)
        original.update(span(0x000E9F00, 256, 0xC3))
        with tempfile.TemporaryDirectory() as d:
            back = {}
            mkuf2.read_hex(hex_file(original, d), back)
            self.assertEqual(back, original)

    def test_an_address_above_64k_gets_an_extended_linear_record(self):
        original = span(0x0003FFF0, 0x20, 0x7E)
        with tempfile.TemporaryDirectory() as d:
            path = hex_file(original, d)
            text = path.read_text()
            back = {}
            mkuf2.read_hex(path, back)
        self.assertEqual(back, original)
        # Type 04 carries the upper 16 bits; the run crosses 0x40000, so the
        # writer has to emit a second one mid-run or everything after the
        # boundary reads back 64 KB low.
        records = [line for line in text.splitlines() if line[7:9] == "04"]
        self.assertEqual([line[9:13] for line in records], ["0003", "0004"])

    def test_the_last_line_is_the_end_of_file_record(self):
        with tempfile.TemporaryDirectory() as d:
            path = hex_file(span(0x00026000, 16), d)
            self.assertEqual(path.read_text().splitlines()[-1], ":00000001FF")

    def test_every_record_carries_a_valid_checksum(self):
        with tempfile.TemporaryDirectory() as d:
            path = hex_file(span(0x00026000, 300), d)
            for line in path.read_text().splitlines():
                raw = bytes.fromhex(line[1:])
                self.assertEqual(sum(raw) & 0xFF, 0, line)

    def test_an_extended_segment_record_offsets_by_sixteen(self):
        # Type 02 is not what Zephyr emits, and the parser accepts it anyway;
        # a wrong shift here would place a whole image 15/16ths too low.
        with tempfile.TemporaryDirectory() as d:
            path = pathlib.Path(d) / "seg.hex"
            path.write_text(":020000021000EC\n:0100000041BE\n:00000001FF\n")
            back = {}
            mkuf2.read_hex(path, back)
            self.assertEqual(back, {0x10000: 0x41})


if __name__ == "__main__":
    unittest.main(verbosity=2)
