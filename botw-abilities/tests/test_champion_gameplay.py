# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
import copy
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1] / 'tools'))
import byml
from build_champion_gameplay import clone_updraft, tuning
from resource_merge import encode_byml


class ChampionGameplayTests(unittest.TestCase):
    def test_updraft_preserves_graph_and_native_components_with_private_modified_references(self):
        settings = tuning()
        donor, actor = settings['donor'], settings['actor']
        actor_path = f'Actor/{donor}.engine__actor__ActorParam.bgyml'
        table_path = f'GameParameter/GameParameterTable/{donor}.engine__actor__GameParameterTable.bgyml'
        param_path = f'GameParameter/AscendingCurrentParam/{donor}.game__object__AscendingCurrentParam.bgyml'
        nodes = {actor_path: {'Category': 'System', 'Components': {'GameParameterTableRef': '?' + table_path,
                    'AIInfoRef': '?AI/AIInfo/native.bgyml', 'ELinkRef': '?Component/ELink/native.bgyml'}},
                 table_path: {'Components': {'AscendingCurrentParam': '?' + param_path}}, param_path: {}}
        source = {name: encode_byml(node, b'YB\x07\0') for name, node in nodes.items()}
        source['AI/AscendingCurrent.root.ainb'] = b'original native graph'
        source['Component/ELink/native.bgyml'] = b'untouched effect data'
        before = copy.deepcopy(source)
        result, origins = clone_updraft(source, settings)
        self.assertEqual(source, before)
        for path in ('AI/AscendingCurrent.root.ainb', 'Component/ELink/native.bgyml'):
            self.assertEqual(result[path], source[path])
            self.assertEqual(origins[path], path)
        new_actor = f'Actor/{actor}.engine__actor__ActorParam.bgyml'
        new_table = f'GameParameter/GameParameterTable/{actor}.engine__actor__GameParameterTable.bgyml'
        new_param = f'GameParameter/AscendingCurrentParam/{actor}.game__object__AscendingCurrentParam.bgyml'
        changed_actor = byml.Byml(result[new_actor]).parse()
        self.assertEqual(changed_actor['Components']['GameParameterTableRef'], '?' + new_table)
        self.assertEqual(changed_actor['Components']['AIInfoRef'], nodes[actor_path]['Components']['AIInfoRef'])
        self.assertEqual(byml.Byml(result[new_table]).parse()['Components']['AscendingCurrentParam'], '?' + new_param)
        self.assertEqual(byml.Byml(result[new_param]).parse()['WindSpeed'], settings['wind_speed'])
        self.assertEqual(set(origins.values()), set(source))
        nodes[param_path]['Unreviewed'] = True
        source[param_path] = encode_byml(nodes[param_path], b'YB\x07\0')
        with self.assertRaisesRegex(ValueError, 'wind defaults'):
            clone_updraft(source, settings)


if __name__ == '__main__':
    unittest.main()
