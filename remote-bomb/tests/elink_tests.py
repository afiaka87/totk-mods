# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Validate installed native effect bytes, stock-user preservation and the Time Bomb explosion."""
import argparse
import hashlib
import json
import sys
import unittest
import byml
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from build_elink import BANKS, RESOURCE, TIMER_BOMB, TIMER_EXPLOSION, USER, TotkZstd
from import_audio import block, parser_budget, transcode, user_blocks


class NativeEffects(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.z = TotkZstd(args.romfs)
        cls.receipt = json.loads((args.candidate/'elink-receipt.json').read_text())
        cls.decoded = args.candidate/'validation'
        cls.decoded.mkdir(exist_ok=True)
        names = cls.decoded/'names.txt'
        names.write_text('BarrelBomb\n'+USER+'\n')
        cls.stock_raw = cls.z.decompress(args.romfs/RESOURCE)
        raw = cls.z.decompress(args.candidate/'romfs'/RESOURCE)
        for name, value in (('stock', cls.stock_raw), ('candidate', raw)):
            binary, text = cls.decoded/(name+'.belnk'), cls.decoded/(name+'.txt')
            binary.write_bytes(value)
            transcode(args.xlink, binary, text, 'EXKing', names)
        cls.original = (cls.decoded/'stock.txt').read_text(encoding='utf-8')
        cls.current = (cls.decoded/'candidate.txt').read_text(encoding='utf-8')
        cls.before, cls.after = user_blocks(cls.original), user_blocks(cls.current)

    def test_stock_users_and_all_private_nonblast_keys(self):
        self.assertEqual(self.original.split('Users {', 1)[0], self.current.split('Users {', 1)[0])
        self.assertEqual(set(self.after)-set(self.before), {USER})
        for name, user in self.before.items():
            self.assertEqual(self.after[name], user, name)
        private = self.after[USER].replace('  '+USER+' {', '  BarrelBomb {', 1)
        call = block(private, r'^      Display_Explosion\[0x[0-9a-f]+\] \{')
        self.assertIn('RuntimeAssetName = "@Blank"', call)
        explosion = block(private, r'^      Chemical_Explosion\[0x[0-9a-f]+\] \{')
        self.assertEqual(explosion, block(self.before[TIMER_BOMB], r'^      Chemical_Explosion\[0x[0-9a-f]+\] \{'))
        self.assertEqual(explosion.count(f'RuntimeAssetName = "{TIMER_EXPLOSION}"'), 1)
        self.assertNotIn('Red = ', explosion)
        restored = private.replace('\n'+explosion, '').replace(
            call, call.replace('RuntimeAssetName = "@Blank"', 'RuntimeAssetName = "Chm_Explosion"'))
        self.assertEqual(restored, self.before['BarrelBomb'])
        self.assertEqual(len(self.before), self.receipt['users_preserved'])

    def test_aggregate_parser_budgets_and_exact_payload_receipt(self):
        previous = dict(line.split('\t') for line in args.previous_floors.read_text().splitlines() if line)
        current = dict(line.split('\t') for line in (args.candidate/'restbl-forces.tsv').read_text().splitlines() if line)
        self.assertEqual(set(current)-set(previous), {RESOURCE[:-3], BANKS[:-3]})
        self.assertTrue(all(current[key] == value for key, value in previous.items()))
        raw = self.z.decompress(args.candidate/'romfs'/RESOURCE)
        table = self.z.decompress(args.romfs/'System/Resource/ResourceSizeTable.Product.121.rsizetable.zs')
        expected = parser_budget(table, RESOURCE[:-3])+max(0, len(raw)-len(self.stock_raw))*10+65536
        self.assertGreaterEqual(int(current[RESOURCE[:-3]]), max(expected, len(raw)+65536))
        self.assertEqual(hashlib.sha256((args.candidate/'romfs'/RESOURCE).read_bytes()).hexdigest(),
                         self.receipt['payload_sha256'])
        stock_banks = self.z.decompress(args.romfs/BANKS)
        banks = self.z.decompress(args.candidate/'romfs'/BANKS)
        expected = parser_budget(table, BANKS[:-3])+max(0, len(banks)-len(stock_banks))*10+65536
        self.assertGreaterEqual(int(current[BANKS[:-3]]), max(expected, len(banks)+65536))
        self.assertEqual(hashlib.sha256((args.candidate/'romfs'/BANKS).read_bytes()).hexdigest(),
                         self.receipt['effect_bank_sha256'])

    def test_only_the_private_user_gains_the_time_bomb_bank(self):
        stock = byml.Byml(self.z.decompress(args.romfs/BANKS)).parse()
        current = byml.Byml(self.z.decompress(args.candidate/'romfs'/BANKS)).parse()
        self.assertEqual(current['BinaryDict'].pop(USER), TIMER_BOMB)
        self.assertEqual(current, stock)
        self.assertIn(TIMER_BOMB, stock['EsetbList'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--romfs', required=True, type=Path)
    parser.add_argument('--candidate', required=True, type=Path)
    parser.add_argument('--previous-floors', required=True, type=Path)
    parser.add_argument('--xlink', required=True, type=Path)
    args, rest = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0]]+rest)
