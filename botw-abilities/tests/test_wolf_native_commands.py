# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Cover the native requests absent from BotW's per-gear command interface."""

from copy import deepcopy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from wolf_native_commands import MOVEMENT, add_native_movement, validate_native_movement


def fixture():
    names = ['Wait'] + [f'Move_{family}_Gear{gear}'
                        for family in ('Normal', 'Attack', 'Escape') for gear in range(1, 5)]
    names += ['Move_AttackEx_Gear1', 'Move_EscapeEx_Gear1']
    graph = {'Blackboard': {'int': [{'Name': 'Other'}, {'Name': 'RequestGear'}]},
             'Commands': [{'Name': name, 'Node Index': i, 'Tags': [name]} for i, name in enumerate(names)],
             'Nodes': [{'Node Index': i, 'Body': {'Animation': name}} for i, name in enumerate(names)]}
    native = {'Blackboard': {'int': [{'Name': 'RequestGear'}]},
              'Commands': [{'Name': name, 'Node Index': 0} for name in MOVEMENT],
              'Nodes': [{'Node Type': 'IntSelector', 'Body': {
                  'Parameter': {'Type': 'int', 'Blackboard Index': 0, 'Select Flag': 'SelectOnUpdateNoFadeout'},
                  'Is Sync': True, 'Force Run': False, 'Child Nodes': []}}]}
    return graph, native


class NativeCommands(unittest.TestCase):
    def test_missing_native_interface_is_added_without_rewriting_originals(self):
        graph, native = fixture()
        before, donor = deepcopy(graph), deepcopy(native)
        with self.assertRaises(KeyError):
            validate_native_movement(graph)
        add_native_movement(graph, native)
        validate_native_movement(graph)
        self.assertEqual(graph['Nodes'][:len(before['Nodes'])], before['Nodes'])
        self.assertTrue(all(command in graph['Commands'] for command in before['Commands']))
        self.assertEqual(native, donor)
        for node in graph['Nodes'][len(before['Nodes']):]:
            self.assertEqual(node['Body']['Parameter']['Blackboard Index'], 1)

    def test_extra_attack_uses_extra_walk_and_normal_faster_gaits(self):
        graph, native = fixture()
        add_native_movement(graph, native)
        command = next(c for c in graph['Commands'] if c['Name'] == 'Move_AttackEx')
        cases = graph['Nodes'][command['Node Index']]['Body']['Child Nodes']
        clips = [graph['Nodes'][case['Node Index']]['Body']['Animation'] for case in cases]
        self.assertEqual(clips, ['Move_AttackEx_Gear1', 'Move_Attack_Gear2',
                                'Move_Attack_Gear3', 'Move_Attack_Gear4', 'Wait'])

    def test_wrong_native_parameter_and_broken_route_are_rejected(self):
        graph, native = fixture()
        native['Blackboard']['int'][0]['Name'] = 'Unrelated'
        with self.assertRaises(ValueError):
            add_native_movement(graph, native)
        graph, native = fixture()
        add_native_movement(graph, native)
        graph['Nodes'][-1]['Body']['Child Nodes'][2]['Node Index'] = 0
        with self.assertRaises(ValueError):
            validate_native_movement(graph)


if __name__ == '__main__':
    unittest.main()
