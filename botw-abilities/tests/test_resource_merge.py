# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
import copy
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path[:0] = [str(Path(__file__).parents[1] / 'tools'), str(ROOT / 'mods/code/totk-remote-bomb/tools')]
import resource_merge as merge
from import_audio import user_blocks, block
from build_champion_effects import clone_carrier, DONOR


def msbt():
    header = bytearray(b'MsgStdBn' + b'\xff\xfe' + b'\0\0\x01\x03\x02\0' + b'\0' * 16)
    label = struct.pack('<III', 1, 1, 12) + b'\x04Base' + struct.pack('<I', 0)
    text = struct.pack('<II', 1, 8) + b'A\0\x0e\0\x02\0\x01\0\0\0B\0\0\0'
    raw = bytes(header)
    for name, data in ((b'LBL1', label), (b'TXT2', text)):
        raw += name + struct.pack('<I', len(data)) + b'\0' * 8 + data + b'\xab' * (-len(data) % 16)
    header = bytearray(raw)
    struct.pack_into('<I', header, 18, len(raw))
    return bytes(header)


class ResourceMergeTests(unittest.TestCase):
    def test_byml_preserves_source_byte_order(self):
        import byml
        for be in (True, False):
            raw = byml.Writer({'value': byml.UInt(0xfedcba98)}, be=be, version=7).get_bytes()
            encoded = merge.encode_byml({'value': byml.UInt(0xfedcba98)}, raw)
            self.assertEqual(encoded[:4], raw[:4])
            self.assertEqual(byml.Byml(encoded).parse(), byml.Byml(raw).parse())

    def test_gamedata_shared_registry_additions_and_save_flags(self):
        base = {'Data': {'Bool': [{'Hash': 1, 'DefaultValue': False}],
                         'Struct': [{'Hash': 12, 'DefaultValue': [{'Hash': 9, 'Value': 1}]}]},
                'MetaData': {'SaveDataSize': [100, 4]}}
        bomb, champion = copy.deepcopy(base), copy.deepcopy(base)
        for edit, number in ((bomb, 20), (champion, 30)):
            edit['Data']['Bool'].append({'Hash': number, 'DefaultValue': True})
            edit['Data']['Struct'][0]['DefaultValue'].append({'Hash': number + 1, 'Value': number})
        result = merge.merge_values(base, {'bomb': bomb, 'champion': champion})
        self.assertEqual([row['Hash'] for row in result['Data']['Bool']], [1, 20, 30])
        self.assertEqual([row['Value'] for row in result['Data']['Struct'][0]['DefaultValue']], [1, 20, 30])
        self.assertEqual(result['MetaData'], base['MetaData'])
        self.assertEqual(len(base['Data']['Bool']), 1)

    def test_independent_edits_preserve_other_member(self):
        base = {'row': {'A': 1, 'B': 2}}
        result = merge.merge_values(base, {'one': {'row': {'A': 3, 'B': 2}},
                                         'two': {'row': {'A': 1, 'B': 4}}})
        self.assertEqual(result, {'row': {'A': 3, 'B': 4}})

    def test_save_capacity_counts_union_once(self):
        base = {'Data': {'Bool': [{'Hash': 1, 'DefaultValue': False, 'SaveFileIndex': 0}]},
                'MetaData': {'AllDataSaveOffset': 100, 'AllDataSaveSize': 200,
                             'SaveDataOffsetPos': [50, 0], 'SaveDataSize': [60, 0],
                             'SaveDirectory': ['progress.sav', '']}}
        data = copy.deepcopy(base['Data'])
        data['Bool'] += [{'Hash': 2, 'SaveFileIndex': 0}, {'Hash': 3, 'SaveFileIndex': -1}]
        metadata = merge.saved_metadata(base, data)
        self.assertEqual(metadata['AllDataSaveOffset'], 116)
        self.assertEqual(metadata['AllDataSaveSize'], 216)
        self.assertEqual(metadata['SaveDataOffsetPos'], [58, 0])
        self.assertEqual(metadata['SaveDataSize'], [68, 0])

    def test_native_save_mutation_and_unknown_allocation_refused(self):
        base = {'Data': {'Bool': [{'Hash': 1, 'DefaultValue': False}]}, 'MetaData': {}}
        with self.assertRaisesRegex(ValueError, 'native save flag changed'):
            merge.saved_metadata(base, {'Bool': [{'Hash': 1, 'DefaultValue': True}]})
        with self.assertRaisesRegex(ValueError, 'allocation recipe'):
            merge.saved_metadata(base, base['Data'] | {'Vector3': [{'Hash': 2}]})

    def test_conflicting_changes_refused(self):
        with self.assertRaisesRegex(ValueError, 'conflicting'):
            merge.merge_values({'A': 0}, {'one': {'A': 1}, 'two': {'A': 2}})

    def test_repeated_and_missing_gamedata_hashes_refused(self):
        base = [{'Hash': 1}, {'Hash': 2}]
        for changed in ([{'Hash': 1}], [{'Hash': 1}, {'Hash': 1}, {'Hash': 2}]):
            with self.assertRaises(ValueError):
                merge.merge_values(base, {'bad': changed})

    def test_native_order_and_shared_addition_preserved(self):
        base = [{'Hash': 20}, {'Hash': 5}]
        one = base + [{'Hash': 7}]
        self.assertEqual(merge.merge_values(base, {'one': one, 'two': one}), one)

    def test_msbt_control_bytes_and_both_additions_preserved(self):
        base = msbt()
        bomb = merge.append_msbt(base, {'Bomb_Name': 'Bomb\0'.encode('utf-16le')})
        champion = merge.append_msbt(base, {'Mipha_Name': 'Grace\0'.encode('utf-16le')})
        raw = merge.merge_msbt(base, {'bomb': bomb, 'champion': champion})
        self.assertEqual(merge.read_msbt(raw)[2], merge.read_msbt(bomb)[2] | merge.read_msbt(champion)[2])
        self.assertEqual(merge.append_msbt(base, {}), base)

    def test_stock_msbt_changes_refused(self):
        base = msbt()
        changed = base.replace(b'A\0\x0e\0', b'C\0\x0e\0')
        with self.assertRaisesRegex(ValueError, 'stock MSBT'):
            merge.merge_msbt(base, {'changed': changed})

    def test_msbt_collision_refused(self):
        base = msbt()
        one = merge.append_msbt(base, {'Same': b'1\0\0\0'})
        two = merge.append_msbt(base, {'Same': b'2\0\0\0'})
        with self.assertRaisesRegex(ValueError, 'conflicting message'):
            merge.merge_msbt(base, {'one': one, 'two': two})

    def test_mals_unchanged_members_remain_exact(self):
        base = merge.write_sarc({'ActorMsg/PouchContent.msbt': msbt(), 'Unrelated.msbt': b'opaque-native-member'})
        members = {}
        for name in ('Bomb', 'Champion'):
            files = merge.sarc_files(base)
            files['ActorMsg/PouchContent.msbt'] = merge.append_msbt(msbt(), {name: b'X\0\0\0'})
            members[name] = merge.write_sarc(files)
        files = merge.sarc_files(merge.merge_mals(base, members))
        self.assertEqual(files['Unrelated.msbt'], b'opaque-native-member')
        self.assertEqual(set(merge.read_msbt(files['ActorMsg/PouchContent.msbt'])[2]), {'Base', 'Bomb', 'Champion'})

    def test_elink_preserves_private_and_stock_users(self):
        base = 'Params {\n}\nUsers {\n  Player {\n    Unknown = 0\n  }\n}\n'
        bomb = base[:-2] + '  RemoteBomb {\n    Unknown = 1\n  }\n}\n'
        champion = base[:-2] + '  Champion {\n    Unknown = 2\n  }\n}\n'
        result = merge.merge_elink_text(base, {'bomb': bomb, 'champion': champion}, user_blocks)
        self.assertEqual(user_blocks(result), user_blocks(bomb) | user_blocks(champion))

    def test_elink_stock_and_parameter_changes_refused(self):
        base = 'Params {\n}\nUsers {\n  Player {\n    Unknown = 0\n  }\n}\n'
        for changed in (base.replace('Unknown = 0', 'Unknown = 1'), base.replace('Params', 'OtherParams')):
            with self.assertRaises(ValueError):
                merge.merge_elink_text(base, {'bad': changed}, user_blocks)

    def test_private_elink_contains_only_added_users(self):
        base = 'Params {\n}\nUsers {\n  Player {\n    Unknown = 0\n  }\n}\n'
        botw = base[:-2] + '  Champion {\n    Unknown = 2\n  }\n}\n'
        result = merge.private_elink_text(base, botw, user_blocks)
        self.assertEqual(user_blocks(result), {'Champion': user_blocks(botw)['Champion']})
        self.assertEqual(result.split('Users {')[0], base.split('Users {')[0])

    def test_private_elink_refuses_stock_or_schema_changes_and_empty_output(self):
        base = 'Params {\n}\nUsers {\n  Player {\n    Unknown = 0\n  }\n}\n'
        for combined in (base, base.replace('Unknown = 0', 'Unknown = 1'),
                         'Params {\n}\nUsers {\n}\n', base.replace('Params', 'ChangedParams')):
            with self.assertRaises(ValueError):
                merge.private_elink_text(base, combined, user_blocks)

    def test_slink_appended_ui_calls_preserve_native_and_private_users(self):
        ui = ('  UI_GlobalSound {\n    Unknown = 0\n    AssetCallTables {\n'
              '      Base[0x1] {\n        EmitCount = 1\n      }\n    }\n  }')
        extra = '      Champion[0x2] {\n        EmitCount = 1\n      }\n'
        changed = ui.replace('    }\n  }',extra+'    }\n  }')
        base = 'Params {\n}\nUsers {\n'+ui+'\n}\n'
        bomb = base[:-2]+'  RemoteBomb {\n    Unknown = 1\n  }\n}\n'
        champion = base.replace(ui,changed)
        result = merge.merge_elink_text(base,{'bomb':bomb,'champion':champion},user_blocks,block)
        self.assertEqual(user_blocks(result),user_blocks(bomb)|{'UI_GlobalSound':changed})
        for bad in (changed.replace('Unknown = 0','Unknown = 1'),
                    changed.replace('Base[0x1]','Renamed[0x1]'),
                    changed.replace('EmitCount = 1','EmitCount = 2',1)):
            with self.assertRaises(ValueError):
                merge.merge_elink_text(base,{'bad':base.replace(ui,bad)},user_blocks,block)
        conflict = champion.replace('Champion[0x2] {\n        EmitCount = 1',
                                    'Champion[0x2] {\n        EmitCount = 2')
        with self.assertRaisesRegex(ValueError,'conflicting UI sound'):
            merge.merge_elink_text(base,{'one':champion,'two':conflict},user_blocks,block)

    def test_carrier_refs_are_private(self):
        import byml
        files = {f'Actor/{DONOR}.engine__actor__ActorParam.bgyml': byml.Writer({'Components': {
            'ELinkRef': f'?Component/ELink/{DONOR}.engine__component__ELinkParam.bgyml'}}, version=7).get_bytes(),
            f'Component/ELink/{DONOR}.engine__component__ELinkParam.bgyml': byml.Writer({
                'UserName': DONOR}, version=7).get_bytes()}
        cloned = merge.sarc_files(clone_carrier(files, 'EftLoc_BotwMiphaHeal'))
        self.assertTrue(all(DONOR not in path for path in cloned))
        values = [byml.Byml(raw).parse() for raw in cloned.values()]
        self.assertIn({'UserName': 'EftLoc_BotwMiphaHeal'}, values)


if __name__ == '__main__':
    unittest.main()
