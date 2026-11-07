# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Check speed selection, mode translation and malformed external input."""

from pathlib import Path
import struct
import sys
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from wolf_movement import speed_rows


def table(records):
    return struct.pack('<I', 24) + b''.join(
        struct.pack('<I48f', key, *values)
        for key, values in sorted((zlib.crc32(name.encode()), values) for name, values in records.items()))


class MovementDataTests(unittest.TestCase):
    def test_actor_record_precedes_fallback(self):
        raw = table({'Companion': range(48), 'Donor': range(48, 96)})
        name, offset, rows = speed_rows(raw, 'Companion', 'Donor')
        self.assertEqual(name, 'Companion')
        self.assertEqual(struct.unpack_from('<I', raw, offset)[0], zlib.crc32(b'Companion'))
        self.assertEqual([row['LinearSpeeds'][0] for row in rows], [8, 16, 24, 0, 32, 40])
        self.assertEqual(rows[2], {'LinearSpeeds': [24, 26, 28, 30],
                                   'AngularSpeeds': [25, 27, 29, 31]})

    def test_missing_actor_uses_fallback(self):
        raw = table({'Companion': range(48), 'Donor': range(48, 96)})
        name, _, rows = speed_rows(raw, 'Missing', 'Donor')
        self.assertEqual(name, 'Donor')
        self.assertEqual(rows[3]['LinearSpeeds'], [48, 50, 52, 54])

    def test_missing_both_fails(self):
        with self.assertRaisesRegex(ValueError, 'Neither actor nor fallback'):
            speed_rows(table({'Donor': range(48)}), 'Missing', 'AlsoMissing')

    def test_bad_dimensions_and_truncation_fail(self):
        raw = table({'Companion': range(48)})
        for malformed in (b'', raw[:3], raw[:4], raw[:-1], struct.pack('<I', 20) + raw[4:]):
            with self.subTest(bytes=len(malformed)), self.assertRaises(ValueError):
                speed_rows(malformed, 'Companion', 'Donor')

    def test_unsorted_and_duplicate_hashes_fail(self):
        raw = table({'Companion': range(48), 'Donor': range(48, 96)})
        for malformed in (raw[:4] + raw[200:] + raw[4:200], raw[:200] + raw[4:200]):
            with self.assertRaisesRegex(ValueError, 'strictly increasing'):
                speed_rows(malformed, 'Companion', 'Donor')

    def test_invalid_speed_values_fail(self):
        for invalid in (float('nan'), float('inf'), -1.0):
            values = list(range(48))
            values[17] = invalid
            with self.subTest(value=invalid), self.assertRaisesRegex(ValueError, 'finite and nonnegative'):
                speed_rows(table({'Companion': values}), 'Companion', 'Donor')


if __name__ == '__main__':
    unittest.main()
