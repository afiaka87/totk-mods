# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1] / 'tools'))
import byml
import build_champion_state as state


def schema():
    return {'Data': {
        kind: [{'Hash': byml.UInt(index), 'DefaultValue': default,
                'ResetTypeValue': byml.Int(16), 'SaveFileIndex': byml.Int(0)}]
        for index, (kind, default) in enumerate((('Bool', False), ('Int', byml.Int(0)),
                                                ('Float', byml.Float(0))), 1)
    } | {'Struct': [{'Hash': byml.UInt(state.hash_name(name)), 'DefaultValue': []}
                    for name in ('IsGet', 'IsGetAnyway')]},
        'MetaData': {'AllDataSaveOffset': byml.Int(100), 'AllDataSaveSize': byml.Int(200),
                     'SaveDataOffsetPos': [byml.Int(20)], 'SaveDataSize': [byml.Int(50)],
                     'SaveDirectory': ['slot_%02d/progress.sav']}}


class ChampionStateTests(unittest.TestCase):
    def test_defaults_persistence_and_item_discovery(self):
        clean = schema()
        data, records = state.extend(clean)
        for hero, region, maximum in state.BLESSINGS:
            keys = records[hero]
            self.assertEqual(keys['actor'], f'Obj_BotwHeroSoul_{region}')
            for kind, field, default in (('Bool', 'Enabled', True), ('Int', 'Charges', maximum),
                                         ('Float', 'Recharge', 0.0), ('Bool', 'IsGet', False),
                                         ('Bool', 'IsGetAnyway', False)):
                row = next(row for row in data['Data'][kind] if row['Hash'] == keys[field])
                self.assertEqual(row['DefaultValue'], default)
                self.assertEqual((row['ResetTypeValue'], row['SaveFileIndex']), (16, 0))
            for name in ('IsGet', 'IsGetAnyway'):
                registry = next(row for row in data['Data']['Struct'] if row['Hash'] == state.hash_name(name))
                self.assertIn({'Hash': state.hash_name(keys['actor']), 'Value': keys[name]}, registry['DefaultValue'])
        self.assertEqual(data['MetaData']['SaveDataSize'][0] - clean['MetaData']['SaveDataSize'][0], 160)
        self.assertEqual(len(clean['Data']['Bool']), 1)

    def test_existing_hash_is_never_overwritten(self):
        clean = schema()
        clean['Data']['Int'][0]['Hash'] = byml.UInt(state.hash_name('BotwChampions.Mipha.Enabled'))
        with self.assertRaisesRegex(ValueError, 'collision'):
            state.extend(clean)

    def test_second_generation_is_refused(self):
        data, _ = state.extend(schema())
        with self.assertRaisesRegex(ValueError, 'collision'):
            state.extend(data)


if __name__ == '__main__':
    unittest.main()
