# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Guard against silently dropping original animation behavior during conversion."""

import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from wolf_as_rules import (UnmappedAnimation, blend_parameter, child_ranges, frame_controller,
                           hold_event, integer_count, previous_tags, random_selector,
                           sequential, trigger_event)


class ConversionGuards(unittest.TestCase):
    def test_loop_enums_are_translated_not_copied(self):
        self.assertEqual([frame_controller({'AnmLoop': mode}, {})['Loop Flags']
                          for mode in (0, 1, 2)], [0, 2, 1])

    def test_unknown_or_nonintegral_counts_are_rejected(self):
        for value in (-2, 1.5, float('nan'), float('inf')):
            with self.subTest(value=value), self.assertRaises(UnmappedAnimation):
                integer_count(value)
        with self.assertRaises(UnmappedAnimation):
            frame_controller({'Unmapped': 1}, {})
        with self.assertRaises(UnmappedAnimation):
            frame_controller({'AnmLoop': 3}, {})

    def test_conversion_preserves_inputs(self):
        template = {'untouched': [4]}
        result = frame_controller({}, template)
        result['untouched'].append(5)
        self.assertEqual(template, {'untouched': [4]})

    def test_ranges_cannot_drop_children_or_reverse_intervals(self):
        for ranges, children in [([], [1]), ([{'Start': 1, 'End': 0}], [1]),
                                  ([{'Start': 0, 'End': float('inf')}], [1])]:
            with self.subTest(ranges=ranges), self.assertRaises(UnmappedAnimation):
                child_ranges(ranges, children)

    def test_unverified_selection_modes_are_rejected(self):
        calls = [
            lambda: blend_parameter({'TypeIndex': 14, 'NoSync': True}, 0, {}),
            lambda: sequential({'SequenceLoop': True}, [1.0], [0]),
            lambda: sequential({}, [0.5], [0]),
            lambda: previous_tags({}, ['Wait'], [0]),
            lambda: random_selector({}, [{'Start': 0.2, 'End': 1.0}], [0]),
        ]
        for index, call in enumerate(calls):
            with self.subTest(index=index), self.assertRaises(UnmappedAnimation):
                call()

    def test_event_22_prevents_animation_changes(self):
        source = {'TypeIndex': 22, 'Value': '', 'StartFrame': 0.0, 'EndFrame': -1.0}
        self.assertEqual(hold_event(source)['Name'], 'NoChangeAS')
        source['TypeIndex'] = 3
        with self.assertRaises(UnmappedAnimation):
            hold_event(source)

    def test_only_verified_relative_trigger_is_translated(self):
        event = {'TypeIndex': 4, 'Frame': -2.0, 'Value': ''}
        self.assertEqual(trigger_event(event)['Start Frame'], -1.0)
        for kind, frame in [(2, -2.0), (4, -3.0), (5, 0.0)]:
            with self.subTest(kind=kind, frame=frame), self.assertRaises(UnmappedAnimation):
                trigger_event({**event, 'TypeIndex': kind, 'Frame': frame})


if __name__ == '__main__':
    unittest.main()
