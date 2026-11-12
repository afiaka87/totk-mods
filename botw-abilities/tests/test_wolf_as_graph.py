# SPDX-License-Identifier: MIT
# Copyright (c) HanSoloBlaster, afiaka87
"""Detect semantic omissions which an ASB writer can round-trip successfully."""

from copy import deepcopy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from check_wolf_as_graph import validate_graph


def fixture():
    path = 'Actor/AS/Test.bas'
    reference = {
        'Actor/ASList/WolfLink.baslist': {'lists': {'ASDefines': {'objects': {
            'first': {'Name': 'Wait', 'Filename': 'Test'},
            'second': {'Name': 'Alias', 'Filename': 'Test'},
        }}}},
        path: {'lists': {'Elements': {'lists': {
            'Element0': {'objects': {'Parameters': {'TypeIndex': 61}, 'Children': {'0': 1, '1': 2}}},
            'Element1': {'objects': {'Parameters': {'TypeIndex': 67, 'FileName': 'start'}}},
            'Element2': {'objects': {'Parameters': {'TypeIndex': 67, 'FileName': 'end'}}},
        }}}},
    }
    graph = {'Commands': [{'Name': 'Wait', 'Node Index': 0}, {'Name': 'Alias', 'Node Index': 0}],
             'Nodes': [
                 {'Node Index': 0, 'GUID': 'root', 'Node Type': 'Sequential', 'Body': {'Child Nodes': [1, 2]}},
                 {'Node Index': 1, 'GUID': 'first', 'Node Type': 'SkeletalAnimation', 'Body': {'Animation': 'start'}},
                 {'Node Index': 2, 'GUID': 'last', 'Node Type': 'SkeletalAnimation', 'Body': {'Animation': 'end'}},
             ]}
    origins = [{'path': path, 'element': f'Element{index}', 'node': index} for index in range(3)]
    return reference, graph, origins


class GraphCoverage(unittest.TestCase):
    def test_complete_alias_graph_passes_without_mutating_inputs(self):
        args = fixture()
        before = deepcopy(args)
        self.assertEqual(validate_graph(*args)['source_edges'], 2)
        self.assertEqual(args, before)

    def test_reordered_or_missing_branch_fails(self):
        for edges in ([2, 1], [1]):
            reference, graph, origins = fixture()
            graph['Nodes'][0]['Body']['Child Nodes'] = edges
            with self.subTest(edges=edges), self.assertRaises(ValueError):
                validate_graph(reference, graph, origins)

    def test_changed_clip_fails(self):
        reference, graph, origins = fixture()
        graph['Nodes'][2]['Body']['Animation'] = 'start'
        with self.assertRaises(ValueError):
            validate_graph(reference, graph, origins)

    def test_alias_target_and_missing_command_fail(self):
        for commands in ([{'Name': 'Wait', 'Node Index': 0}],
                         [{'Name': 'Wait', 'Node Index': 0}, {'Name': 'Alias', 'Node Index': 1}]):
            reference, graph, origins = fixture()
            graph['Commands'] = commands
            with self.subTest(commands=commands), self.assertRaises(ValueError):
                validate_graph(reference, graph, origins)

    def test_orphan_node_fails(self):
        reference, graph, origins = fixture()
        graph['Nodes'].append({'Node Index': 3, 'GUID': 'orphan', 'Node Type': 'DummyAnimation', 'Body': {}})
        with self.assertRaises(ValueError):
            validate_graph(reference, graph, origins)

    def test_dropped_source_event_fails(self):
        reference, graph, origins = fixture()
        original = reference['Actor/AS/Test.bas']['lists']['Elements']['lists']['Element1']
        original['lists'] = {'Extend': {'lists': {'HoldEvents': {'objects': {
            'event': {'TypeIndex': 22, 'Value': '', 'StartFrame': 0.0, 'EndFrame': -1.0},
        }}}}}
        with self.assertRaises(ValueError):
            validate_graph(reference, graph, origins)


if __name__ == '__main__':
    unittest.main()
