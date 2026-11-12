# SPDX-License-Identifier: MIT
# Copyright (c) HanSoloBlaster, afiaka87
"""Original exact/default/excluded transitions must survive native first-match dispatch."""

from copy import deepcopy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from check_wolf_as_graph import validate_transitions
from wolf_as_rules import crossfade_transitions, UnmappedAnimation


def fixture():
    group = {'objects': {'CFPre': {'Name': 'Wait'}, 'CFExcepts': {'Name_0': 'Dead'}},
             'lists': {'CFPosts': {'objects': {
                 'default': {'Name': '', 'Frame': 12.0, 'StartFrameRate': -1.0},
                 'named': {'Name': 'Damage', 'Frame': 2.0, 'StartFrameRate': 0.0},
                 'excluded': {'Name': 'Dead', 'Frame': 99.0, 'StartFrameRate': 0.0},
             }}}}
    source = {'lists': {'CFDefines': {'lists': {'group': group}}}}
    graph = {'Commands': [{'Name': name} for name in ('Wait', 'Damage', 'Dead', 'Move')],
             'Transitions': crossfade_transitions([group])[0]}
    return source, graph


class CrossfadeRules(unittest.TestCase):
    def test_all_pairs_and_unlisted_commands_match_without_mutating_source(self):
        source, graph = fixture()
        before = deepcopy(source)
        self.assertEqual(validate_transitions(source, graph),
                         {'transition_pairs': 25, 'matched_transition_pairs': 4})
        self.assertEqual(source, before)

    def test_exclusions_use_two_native_rows_instead_of_expanding_command_names(self):
        _, graph = fixture()
        rows = graph['Transitions'][0]['Transitions']
        self.assertEqual(len(rows), 4)
        self.assertEqual([row['Next Command'] for row in rows], ['Damage', 'Damage', '', ''])
        self.assertEqual(rows[-1]['Command Group'], ['Dead'])

    def test_default_before_specific_is_rejected(self):
        source, graph = fixture()
        rows = graph['Transitions'][0]['Transitions']
        rows[:] = rows[2:] + rows[:2]
        with self.assertRaisesRegex(ValueError, 'Wait -> Damage'):
            validate_transitions(source, graph)

    def test_missing_exclusion_is_rejected(self):
        source, graph = fixture()
        for row in graph['Transitions'][0]['Transitions']:
            row.pop('Command Group', None)
        with self.assertRaisesRegex(ValueError, 'Wait -> Dead'):
            validate_transitions(source, graph)

    def test_missing_pair_continuation_is_rejected(self):
        source, graph = fixture()
        graph['Transitions'][0]['Transitions'][0]['Allow Multiple Matches'] = False
        with self.assertRaisesRegex(ValueError, 'Wait -> Damage'):
            validate_transitions(source, graph)

    def test_start_rate_stays_explicitly_unresolved(self):
        source, _ = fixture()
        _, starts = crossfade_transitions(source['lists']['CFDefines']['lists'].values())
        self.assertEqual([(row['next'], row['value']) for row in starts], [('Damage', 0.0), ('', -1.0)])

    def test_fade_duration_includes_original_extra_frame_only_for_positive_values(self):
        for original, native in ((0.0, 0.0), (2.0, 3.0), (12.0, 13.0)):
            source, _ = fixture()
            group = source['lists']['CFDefines']['lists']['group']
            group['lists']['CFPosts']['objects']['named']['Frame'] = original
            transitions, _ = crossfade_transitions([group])
            with self.subTest(original=original):
                self.assertEqual(transitions[0]['Transitions'][0]['Value'], native)

    def test_only_first_post_can_provide_original_wildcard_fallback(self):
        source, graph = fixture()
        group = source['lists']['CFDefines']['lists']['group']
        posts = group['lists']['CFPosts']['objects']
        group['lists']['CFPosts']['objects'] = {key: posts[key] for key in ('named', 'default', 'excluded')}
        graph['Transitions'], _ = crossfade_transitions([group])
        self.assertEqual(len(graph['Transitions'][0]['Transitions']), 2)
        self.assertEqual(validate_transitions(source, graph)['matched_transition_pairs'], 1)

    def test_omitting_original_extra_frame_is_rejected(self):
        source, graph = fixture()
        graph['Transitions'][0]['Transitions'][0]['Value'] = 2.0
        with self.assertRaisesRegex(ValueError, 'Wait -> Damage'):
            validate_transitions(source, graph)

    def test_unknown_fields_duplicate_names_and_nonfinite_times_are_rejected(self):
        for change in ('unknown', 'duplicate', 'nonfinite', 'negative'):
            source, _ = fixture()
            group = source['lists']['CFDefines']['lists']['group']
            posts = group['lists']['CFPosts']['objects']
            if change == 'unknown':
                posts['default']['new_field'] = True
            elif change == 'duplicate':
                posts['named']['Name'] = ''
            elif change == 'nonfinite':
                posts['default']['Frame'] = float('nan')
            else:
                posts['default']['Frame'] = -1.0
            with self.subTest(change=change), self.assertRaises(UnmappedAnimation):
                crossfade_transitions([group])


if __name__ == '__main__':
    unittest.main()
